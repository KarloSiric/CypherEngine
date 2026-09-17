//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp
//  Purpose: Implements the backend-neutral cooked material resource contract.
//  Details: Writers sort named values into one canonical representation and
//           readers validate every offset, string, value, hash, and layout before
//           publishing immutable borrowed views into the caller-owned file bytes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CookedMaterial.h"

#include "CypherCommon_ByteReader.h"
#include "CypherCommon_ByteWriter.h"
#include "CypherCommon_DataValidation.h"
#include "CypherCommon_MemoryOps.h"

#include <cmath>

namespace cypher::common
{

namespace
{

inline constexpr flags32_t CY_COOKED_MATERIAL_V1_KNOWN_FLAGS =
    COOKED_MATERIAL_FLAG_NONE; // Unknown persisted material flags are rejected.
inline constexpr flags32_t CY_COOKED_MATERIAL_V2_KNOWN_FLAGS =
    COOKED_MATERIAL_FLAG_TWO_SIDED |
    COOKED_MATERIAL_FLAG_CASTS_SHADOWS |
    COOKED_MATERIAL_FLAG_RECEIVES_SHADOWS;

struct material_string_ref_t {
    u32 iOffset{ 0u };  // Byte offset into MTST.
    u32 cchLength{ 0u }; // Bytes before the required NUL terminator.
};

struct material_texture_record_t {
    material_string_ref_t binding{}; // Shader sampler name.
    material_string_ref_t texture{}; // Referenced .cytex path.
};

struct material_parameter_record_t {
    material_string_ref_t name{}; // Shader parameter name in MTST.
    render_material_parameter_type_t type{
        render_material_parameter_type_t::SCALAR
    };
    u32 nComponents{ 0u }; // Active numeric values; zero for Boolean.
    f64 values[CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS]{}; // Canonical payload.
};

struct material_metadata_t {
    flags32_t flags{ COOKED_MATERIAL_FLAG_NONE }; // Persisted material flags.
    u32 nTextures{ 0u };                          // Following texture records.
    u32 nParameters{ 0u };                        // Following parameter records.
    material_string_ref_t shader{};               // Shader path in MTST.
    u32 cbStringTable{ 0u };                      // Exact MTST payload size.
};

struct canonical_material_t {
    string_view_t shader{}; // Borrowed validated shader path.
    cooked_material_texture_source_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    cooked_material_parameter_source_t
        parameters[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    usize nTextures{ 0u };   // Sorted active texture bindings.
    usize nParameters{ 0u }; // Sorted active parameters.
    usize cbStringTable{ 0u }; // Exact canonical MTST size.
    flags32_t flags{ COOKED_MATERIAL_FLAG_NONE }; // Validated flags.
};

template <usize nLength>
CYPHER_NODISCARD constexpr string_view_t MaterialText(
    const char ( &text )[nLength] ) noexcept
{
    return { text, nLength - 1u };
}

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

CYPHER_NODISCARD bool_t AddStringSize(
    string_view_t value,
    usize &cbStrings ) noexcept
{
    usize cbWithTerminator = 0u;
    return StringView_IsValid( value ) &&
           CheckedAdd( value.cchLength, 1u, cbWithTerminator ) &&
           CheckedAdd( cbStrings, cbWithTerminator, cbStrings ) &&
           cbStrings <= CY_COOKED_MATERIAL_MAX_STRING_TABLE_SIZE;
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

CYPHER_NODISCARD f64 CanonicalNumber( f64 value ) noexcept
{
    // Collapse negative zero so semantically equal inputs serialize identically.
    return value == 0.0 ? 0.0 : value;
}

CYPHER_NODISCARD bool_t IsParameterTypeValid(
    render_material_parameter_type_t type ) noexcept
{
    return type == render_material_parameter_type_t::BOOL ||
           type == render_material_parameter_type_t::SCALAR ||
           type == render_material_parameter_type_t::VECTOR;
}

CYPHER_NODISCARD cooked_material_status_t ValidateParameter(
    const cooked_material_parameter_source_t &parameter ) noexcept
{
    if ( !DataValidation_Succeeded(
             DataValidation_CheckAsciiIdentifier(
                 parameter.name,
                 CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ||
         !IsParameterTypeValid( parameter.type ) ) {
        return cooked_material_status_t::INVALID_PARAMETER;
    }

    usize nValues = 0u;
    switch ( parameter.type ) {
        case render_material_parameter_type_t::BOOL:
            if ( parameter.nComponents != 0u ) {
                return cooked_material_status_t::INVALID_PARAMETER;
            }
            break;
        case render_material_parameter_type_t::SCALAR:
            if ( parameter.nComponents != 1u ) {
                return cooked_material_status_t::INVALID_PARAMETER;
            }
            nValues = 1u;
            break;
        case render_material_parameter_type_t::VECTOR:
            if ( parameter.nComponents <
                     CY_RENDER_MATERIAL_VECTOR_MIN_COMPONENTS ||
                 parameter.nComponents >
                     CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS ) {
                return cooked_material_status_t::INVALID_PARAMETER;
            }
            nValues = parameter.nComponents;
            break;
    }

    for ( usize iValue = 0u; iValue < nValues; ++iValue ) {
        if ( !std::isfinite( parameter.values[iValue] ) ) {
            return cooked_material_status_t::NON_FINITE_VALUE;
        }
    }
    return cooked_material_status_t::OK;
}

void SortTextures(
    cooked_material_texture_source_t *pTextures,
    usize nTextures ) noexcept
{
    // Counts are capped at 32, making stable insertion sort simple and sufficient.
    for ( usize iTexture = 1u; iTexture < nTextures; ++iTexture ) {
        const cooked_material_texture_source_t value = pTextures[iTexture];
        usize iInsert = iTexture;
        while ( iInsert > 0u &&
                StringView_Compare(
                    value.binding,
                    pTextures[iInsert - 1u].binding ) < 0 ) {
            pTextures[iInsert] = pTextures[iInsert - 1u];
            --iInsert;
        }
        pTextures[iInsert] = value;
    }
}

void SortParameters(
    cooked_material_parameter_source_t *pParameters,
    usize nParameters ) noexcept
{
    // Canonical name order enables deterministic bytes and binary-search lookup.
    for ( usize iParameter = 1u;
          iParameter < nParameters;
          ++iParameter ) {
        const cooked_material_parameter_source_t value =
            pParameters[iParameter];
        usize iInsert = iParameter;
        while ( iInsert > 0u &&
                StringView_Compare(
                    value.name,
                    pParameters[iInsert - 1u].name ) < 0 ) {
            pParameters[iInsert] = pParameters[iInsert - 1u];
            --iInsert;
        }
        pParameters[iInsert] = value;
    }
}

CYPHER_NODISCARD cooked_material_status_t CanonicalizeMaterial(
    const cooked_material_source_t &material,
    canonical_material_t &canonical,
    cooked_material_result_t *pResult ) noexcept
{
    // Validate and copy into bounded scratch storage before sorting caller data.
    if ( !Span_IsValid( material.textures ) ||
         !Span_IsValid( material.parameters ) ) {
        return cooked_material_status_t::INVALID_ARGUMENT;
    }
    if ( material.textures.nCount > CY_RENDER_MATERIAL_MAX_TEXTURES ) {
        return cooked_material_status_t::TEXTURE_LIMIT_EXCEEDED;
    }
    if ( material.parameters.nCount > CY_RENDER_MATERIAL_MAX_PARAMETERS ) {
        return cooked_material_status_t::PARAMETER_LIMIT_EXCEEDED;
    }
    if ( ( material.flags & ~CY_COOKED_MATERIAL_V1_KNOWN_FLAGS ) != 0u ) {
        return cooked_material_status_t::INVALID_FLAGS;
    }
    if ( !DataValidation_Succeeded(
             DataValidation_CheckResourcePath(
                 material.shader,
                 MaterialText( ".cyshader" ),
                 CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
        return cooked_material_status_t::INVALID_SHADER_PATH;
    }

    canonical.shader = material.shader;
    canonical.nTextures = material.textures.nCount;
    canonical.nParameters = material.parameters.nCount;
    canonical.flags = material.flags;

    usize cbStrings = 0u;
    if ( !AddStringSize( canonical.shader, cbStrings ) ) {
        return cooked_material_status_t::INVALID_STRING;
    }

    for ( usize iTexture = 0u;
          iTexture < canonical.nTextures;
          ++iTexture ) {
        const cooked_material_texture_source_t &texture =
            material.textures.pData[iTexture];
        if ( !DataValidation_Succeeded(
                 DataValidation_CheckAsciiIdentifier(
                     texture.binding,
                     CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ||
             !DataValidation_Succeeded(
                 DataValidation_CheckResourcePath(
                     texture.texture,
                     MaterialText( ".cytex" ),
                     CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
            if ( pResult != nullptr ) {
                pResult->iTexture = iTexture;
            }
            return cooked_material_status_t::INVALID_TEXTURE;
        }
        if ( !AddStringSize( texture.binding, cbStrings ) ||
             !AddStringSize( texture.texture, cbStrings ) ) {
            if ( pResult != nullptr ) {
                pResult->iTexture = iTexture;
            }
            return cooked_material_status_t::INVALID_STRING;
        }
        canonical.textures[iTexture] = texture;
    }

    for ( usize iParameter = 0u;
          iParameter < canonical.nParameters;
          ++iParameter ) {
        const cooked_material_parameter_source_t &parameter =
            material.parameters.pData[iParameter];
        const cooked_material_status_t status = ValidateParameter( parameter );
        if ( status != cooked_material_status_t::OK ) {
            if ( pResult != nullptr ) {
                pResult->iParameter = iParameter;
            }
            return status;
        }
        if ( !AddStringSize( parameter.name, cbStrings ) ) {
            if ( pResult != nullptr ) {
                pResult->iParameter = iParameter;
            }
            return cooked_material_status_t::INVALID_STRING;
        }
        canonical.parameters[iParameter] = parameter;
    }

    // Serialized named records are always ascending and duplicate-free.
    SortTextures( canonical.textures, canonical.nTextures );
    SortParameters( canonical.parameters, canonical.nParameters );

    for ( usize iTexture = 1u;
          iTexture < canonical.nTextures;
          ++iTexture ) {
        if ( StringView_Equals(
                 canonical.textures[iTexture - 1u].binding,
                 canonical.textures[iTexture].binding ) ) {
            if ( pResult != nullptr ) {
                pResult->iTexture = iTexture;
            }
            return cooked_material_status_t::DUPLICATE_NAME;
        }
    }
    for ( usize iParameter = 1u;
          iParameter < canonical.nParameters;
          ++iParameter ) {
        if ( StringView_Equals(
                 canonical.parameters[iParameter - 1u].name,
                 canonical.parameters[iParameter].name ) ) {
            if ( pResult != nullptr ) {
                pResult->iParameter = iParameter;
            }
            return cooked_material_status_t::DUPLICATE_NAME;
        }
    }

    canonical.cbStringTable = cbStrings;
    return cooked_material_status_t::OK;
}

CYPHER_NODISCARD bool_t PrepareCanonicalLayout(
    const canonical_material_t &material,
    cooked_chunk_desc_t ( &chunks )[2],
    usize &cbFileOut ) noexcept
{
    // Materials use exactly two chunks: fixed metadata followed by its string table.
    usize iOffset = CookedResource_PrefixSize( 2u );
    if ( iOffset == 0u ||
         !Cy_AlignUpChecked(
             iOffset,
             CY_COOKED_MATERIAL_METADATA_ALIGNMENT,
             iOffset ) ) {
        return CY_FALSE;
    }

    const usize cbMetadata = CookedMaterial_MetadataSize(
        static_cast<u32>( material.nTextures ),
        static_cast<u32>( material.nParameters ) );
    if ( cbMetadata == 0u ) {
        return CY_FALSE;
    }
    chunks[0].chunkType = CY_COOKED_MATERIAL_METADATA_CHUNK;
    chunks[0].nAlignment = CY_COOKED_MATERIAL_METADATA_ALIGNMENT;
    chunks[0].iOffset = iOffset;
    chunks[0].cbStored = cbMetadata;
    chunks[0].cbDecoded = cbMetadata;
    chunks[0].flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    if ( !CheckedAdd( iOffset, cbMetadata, iOffset ) ) {
        return CY_FALSE;
    }

    chunks[1].chunkType = CY_COOKED_MATERIAL_STRING_CHUNK;
    chunks[1].nAlignment = CY_COOKED_MATERIAL_STRING_ALIGNMENT;
    chunks[1].iOffset = iOffset;
    chunks[1].cbStored = material.cbStringTable;
    chunks[1].cbDecoded = material.cbStringTable;
    chunks[1].flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    return CheckedAdd( iOffset, material.cbStringTable, cbFileOut );
}

CYPHER_NODISCARD material_string_ref_t NextStringRef(
    string_view_t value,
    usize &iString ) noexcept
{
    // Strings are emitted once, in metadata traversal order, including each NUL.
    const material_string_ref_t ref{
        static_cast<u32>( iString ),
        static_cast<u32>( value.cchLength )
    };
    iString += value.cchLength + 1u;
    return ref;
}

CYPHER_NODISCARD bool_t WriteStringRef(
    byte_writer_t &writer,
    material_string_ref_t ref ) noexcept
{
    return ByteWriter_WriteU32( &writer, ref.iOffset ) &&
           ByteWriter_WriteU32( &writer, ref.cchLength );
}

CYPHER_NODISCARD bool_t WriteMetadata(
    const canonical_material_t &material,
    byte_span_t output ) noexcept
{
    // Offset records are computed before any field is serialized.
    material_texture_record_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    material_parameter_record_t
        parameters[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};

    usize iString = 0u;
    const material_string_ref_t shader =
        NextStringRef( material.shader, iString );
    for ( usize iTexture = 0u;
          iTexture < material.nTextures;
          ++iTexture ) {
        textures[iTexture].binding = NextStringRef(
            material.textures[iTexture].binding,
            iString );
        textures[iTexture].texture = NextStringRef(
            material.textures[iTexture].texture,
            iString );
    }
    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        const cooked_material_parameter_source_t &source =
            material.parameters[iParameter];
        material_parameter_record_t &record = parameters[iParameter];
        record.name = NextStringRef( source.name, iString );
        record.type = source.type;
        record.nComponents = source.nComponents;
        if ( source.type == render_material_parameter_type_t::BOOL ) {
            record.values[0] = source.bValue ? 1.0 : 0.0;
        } else {
            for ( usize iValue = 0u;
                  iValue < source.nComponents;
                  ++iValue ) {
                record.values[iValue] = CanonicalNumber(
                    source.values[iValue] );
            }
        }
    }
    if ( iString != material.cbStringTable ) {
        return CY_FALSE;
    }

    byte_writer_t writer{};
    if ( !ByteWriter_Init(
             &writer,
             output,
             data_byte_order_t::LITTLE ) ||
         !ByteWriter_WriteU32( &writer, CY_COOKED_MATERIAL_METADATA_MAGIC ) ||
         !ByteWriter_WriteU32(
             &writer,
             CY_COOKED_MATERIAL_METADATA_VERSION ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( CY_COOKED_MATERIAL_METADATA_HEADER_SIZE ) ) ||
         !ByteWriter_WriteU32( &writer, material.flags ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( material.nTextures ) ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( material.nParameters ) ) ||
         !WriteStringRef( writer, shader ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( material.cbStringTable ) ) ||
         !ByteWriter_WriteU32( &writer, 0u ) ||
         !ByteWriter_WriteU32( &writer, 0u ) ||
         !ByteWriter_WriteU32( &writer, 0u ) ) {
        return CY_FALSE;
    }

    for ( usize iTexture = 0u;
          iTexture < material.nTextures;
          ++iTexture ) {
        if ( !WriteStringRef( writer, textures[iTexture].binding ) ||
             !WriteStringRef( writer, textures[iTexture].texture ) ) {
            return CY_FALSE;
        }
    }
    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        const material_parameter_record_t &parameter =
            parameters[iParameter];
        if ( !WriteStringRef( writer, parameter.name ) ||
             !ByteWriter_WriteU32(
                 &writer,
                 static_cast<u32>( parameter.type ) ) ||
             !ByteWriter_WriteU32( &writer, parameter.nComponents ) ) {
            return CY_FALSE;
        }
        for ( usize iValue = 0u;
              iValue < CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS;
              ++iValue ) {
            if ( !ByteWriter_WriteF64(
                     &writer,
                     parameter.values[iValue] ) ) {
                return CY_FALSE;
            }
        }
    }
    return ByteWriter_BytesWritten( &writer ) == output.nCount;
}

CYPHER_NODISCARD bool_t WriteStrings(
    const canonical_material_t &material,
    byte_span_t output ) noexcept
{
    byte_writer_t writer{};
    if ( !ByteWriter_Init(
             &writer,
             output,
             data_byte_order_t::LITTLE ) ||
         !ByteWriter_WriteString( &writer, material.shader, CY_TRUE ) ) {
        return CY_FALSE;
    }
    for ( usize iTexture = 0u;
          iTexture < material.nTextures;
          ++iTexture ) {
        if ( !ByteWriter_WriteString(
                 &writer,
                 material.textures[iTexture].binding,
                 CY_TRUE ) ||
             !ByteWriter_WriteString(
                 &writer,
                 material.textures[iTexture].texture,
                 CY_TRUE ) ) {
            return CY_FALSE;
        }
    }
    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        if ( !ByteWriter_WriteString(
                 &writer,
                 material.parameters[iParameter].name,
                 CY_TRUE ) ) {
            return CY_FALSE;
        }
    }
    return ByteWriter_BytesWritten( &writer ) == output.nCount;
}

CYPHER_NODISCARD bool_t OutputOverlapsMaterial(
    byte_span_t output,
    usize cbRequired,
    const cooked_material_source_t &source,
    const canonical_material_t &material ) noexcept
{
    if ( Cy_MemRangesOverlap(
             output.pData,
             cbRequired,
             &source,
             sizeof( source ) ) ||
         Cy_MemRangesOverlap(
             output.pData,
             cbRequired,
             source.textures.pData,
             source.textures.nCount *
                 sizeof( cooked_material_texture_source_t ) ) ||
         Cy_MemRangesOverlap(
             output.pData,
             cbRequired,
             source.parameters.pData,
             source.parameters.nCount *
                 sizeof( cooked_material_parameter_source_t ) ) ||
         Cy_MemRangesOverlap(
             output.pData,
             cbRequired,
             material.shader.pData,
             material.shader.cchLength ) ) {
        return CY_TRUE;
    }

    for ( usize iTexture = 0u;
          iTexture < material.nTextures;
          ++iTexture ) {
        if ( Cy_MemRangesOverlap(
                 output.pData,
                 cbRequired,
                 material.textures[iTexture].binding.pData,
                 material.textures[iTexture].binding.cchLength ) ||
             Cy_MemRangesOverlap(
                 output.pData,
                 cbRequired,
                 material.textures[iTexture].texture.pData,
                 material.textures[iTexture].texture.cchLength ) ) {
            return CY_TRUE;
        }
    }
    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        if ( Cy_MemRangesOverlap(
                 output.pData,
                 cbRequired,
                 material.parameters[iParameter].name.pData,
                 material.parameters[iParameter].name.cchLength ) ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ReadStringRef(
    byte_reader_t &reader,
    material_string_ref_t &ref ) noexcept
{
    return ByteReader_ReadU32( &reader, &ref.iOffset ) &&
           ByteReader_ReadU32( &reader, &ref.cchLength );
}

CYPHER_NODISCARD bool_t ReadMetadataHeader(
    byte_reader_t &reader,
    material_metadata_t &metadata ) noexcept
{
    u32 magic = 0u;
    u32 version = 0u;
    u32 cbHeader = 0u;
    u32 reserved0 = 0u;
    u32 reserved1 = 0u;
    u32 reserved2 = 0u;
    return ByteReader_ReadU32( &reader, &magic ) &&
           ByteReader_ReadU32( &reader, &version ) &&
           ByteReader_ReadU32( &reader, &cbHeader ) &&
           ByteReader_ReadU32( &reader, &metadata.flags ) &&
           ByteReader_ReadU32( &reader, &metadata.nTextures ) &&
           ByteReader_ReadU32( &reader, &metadata.nParameters ) &&
           ReadStringRef( reader, metadata.shader ) &&
           ByteReader_ReadU32( &reader, &metadata.cbStringTable ) &&
           ByteReader_ReadU32( &reader, &reserved0 ) &&
           ByteReader_ReadU32( &reader, &reserved1 ) &&
           ByteReader_ReadU32( &reader, &reserved2 ) &&
           magic == CY_COOKED_MATERIAL_METADATA_MAGIC &&
           version == CY_COOKED_MATERIAL_METADATA_VERSION &&
           cbHeader == CY_COOKED_MATERIAL_METADATA_HEADER_SIZE &&
           reserved0 == 0u && reserved1 == 0u && reserved2 == 0u;
}

CYPHER_NODISCARD bool_t ReadTextureRecord(
    byte_reader_t &reader,
    material_texture_record_t &texture ) noexcept
{
    return ReadStringRef( reader, texture.binding ) &&
           ReadStringRef( reader, texture.texture );
}

CYPHER_NODISCARD bool_t ReadParameterRecord(
    byte_reader_t &reader,
    material_parameter_record_t &parameter ) noexcept
{
    u32 type = 0u;
    if ( !ReadStringRef( reader, parameter.name ) ||
         !ByteReader_ReadU32( &reader, &type ) ||
         !ByteReader_ReadU32( &reader, &parameter.nComponents ) ) {
        return CY_FALSE;
    }
    parameter.type = static_cast<render_material_parameter_type_t>( type );
    for ( usize iValue = 0u;
          iValue < CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS;
          ++iValue ) {
        if ( !ByteReader_ReadF64( &reader, &parameter.values[iValue] ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ResolveString(
    binary_block_t strings,
    material_string_ref_t ref,
    string_view_t &valueOut ) noexcept
{
    if ( ref.cchLength == 0u ||
         ref.iOffset >= strings.cbSize ||
         ref.cchLength >= strings.cbSize - ref.iOffset ||
         strings.pData[ref.iOffset + ref.cchLength] != 0u ) {
        return CY_FALSE;
    }
    for ( usize iCharacter = 0u;
          iCharacter < ref.cchLength;
          ++iCharacter ) {
        if ( strings.pData[ref.iOffset + iCharacter] == 0u ) {
            return CY_FALSE;
        }
    }
    valueOut = {
        reinterpret_cast<const char *>( strings.pData + ref.iOffset ),
        ref.cchLength
    };
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ConsumeCanonicalString(
    binary_block_t strings,
    material_string_ref_t ref,
    usize &iExpected,
    string_view_t &valueOut ) noexcept
{
    if ( ref.iOffset != iExpected ||
         !ResolveString( strings, ref, valueOut ) ) {
        return CY_FALSE;
    }
    return CheckedAdd(
        iExpected,
        static_cast<usize>( ref.cchLength ) + 1u,
        iExpected );
}

CYPHER_NODISCARD cooked_material_status_t DecodeParameter(
    const material_parameter_record_t &record,
    cooked_material_parameter_view_t &parameter ) noexcept
{
    if ( !IsParameterTypeValid( record.type ) ) {
        return cooked_material_status_t::INVALID_PARAMETER;
    }
    parameter.type = record.type;
    parameter.nComponents = record.nComponents;

    usize nActiveValues = 0u;
    if ( record.type == render_material_parameter_type_t::BOOL ) {
        if ( record.nComponents != 0u ||
             ( record.values[0] != 0.0 && record.values[0] != 1.0 ) ) {
            return cooked_material_status_t::INVALID_PARAMETER;
        }
        parameter.bValue = record.values[0] != 0.0;
        nActiveValues = 1u;
    } else if ( record.type == render_material_parameter_type_t::SCALAR ) {
        if ( record.nComponents != 1u ) {
            return cooked_material_status_t::INVALID_PARAMETER;
        }
        nActiveValues = 1u;
    } else {
        if ( record.nComponents < CY_RENDER_MATERIAL_VECTOR_MIN_COMPONENTS ||
             record.nComponents > CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS ) {
            return cooked_material_status_t::INVALID_PARAMETER;
        }
        nActiveValues = record.nComponents;
    }

    for ( usize iValue = 0u;
          iValue < CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS;
          ++iValue ) {
        if ( !std::isfinite( record.values[iValue] ) ) {
            return cooked_material_status_t::NON_FINITE_VALUE;
        }
        // The writer normalizes negative zero so semantically identical values
        // have exactly one serialized representation.
        if ( record.values[iValue] == 0.0 &&
             std::signbit( record.values[iValue] ) ) {
            return cooked_material_status_t::NON_CANONICAL_LAYOUT;
        }
        if ( iValue >= nActiveValues && record.values[iValue] != 0.0 ) {
            return cooked_material_status_t::INVALID_PARAMETER;
        }
        parameter.values[iValue] = record.values[iValue];
    }
    return cooked_material_status_t::OK;
}

} // namespace

usize CookedMaterial_MetadataSize(
    u32 nTextures,
    u32 nParameters ) noexcept
{
    if ( nTextures > CY_RENDER_MATERIAL_MAX_TEXTURES ||
         nParameters > CY_RENDER_MATERIAL_MAX_PARAMETERS ) {
        return 0u;
    }
    return CY_COOKED_MATERIAL_METADATA_HEADER_SIZE +
           static_cast<usize>( nTextures ) *
               CY_COOKED_MATERIAL_TEXTURE_RECORD_SIZE +
           static_cast<usize>( nParameters ) *
               CY_COOKED_MATERIAL_PARAMETER_RECORD_SIZE;
}

usize CookedMaterial_MetadataSizeV2(
    u32 nFeatures,
    u32 nTextures,
    u32 nParameters ) noexcept
{
    if ( nFeatures > CY_RENDER_MATERIAL_MAX_FEATURES ||
         nTextures > CY_RENDER_MATERIAL_MAX_TEXTURES ||
         nParameters > CY_RENDER_MATERIAL_MAX_PARAMETERS ) {
        return 0u;
    }
    return CY_COOKED_MATERIAL_METADATA_HEADER_SIZE_V2 +
           static_cast<usize>( nFeatures ) *
               CY_COOKED_MATERIAL_FEATURE_RECORD_SIZE_V2 +
           static_cast<usize>( nTextures ) *
               CY_COOKED_MATERIAL_TEXTURE_RECORD_SIZE_V2 +
           static_cast<usize>( nParameters ) *
               CY_COOKED_MATERIAL_PARAMETER_RECORD_SIZE_V2;
}

usize CookedMaterial_RequiredSize(
    const cooked_material_source_t &material ) noexcept
{
    canonical_material_t canonical{};
    if ( CanonicalizeMaterial(
             material,
             canonical,
             nullptr ) != cooked_material_status_t::OK ) {
        return 0u;
    }
    cooked_chunk_desc_t chunks[2]{};
    usize cbFile = 0u;
    return PrepareCanonicalLayout( canonical, chunks, cbFile )
        ? cbFile
        : 0u;
}

cooked_material_result_t CookedMaterial_Write(
    const cooked_material_source_t &material,
    content_hash_t sourceHash,
    byte_span_t output ) noexcept
{
    cooked_material_result_t result{};
    if ( !Span_IsValid( output ) ) {
        result.status = cooked_material_status_t::INVALID_ARGUMENT;
        return result;
    }

    // Canonicalization is transactional and does not mutate caller-owned arrays.
    canonical_material_t canonical{};
    result.status = CanonicalizeMaterial(
        material,
        canonical,
        &result );
    if ( result.status != cooked_material_status_t::OK ) {
        return result;
    }

    cooked_chunk_desc_t chunks[2]{};
    if ( !PrepareCanonicalLayout(
             canonical,
             chunks,
             result.cbRequired ) ) {
        result.status = cooked_material_status_t::INVALID_METADATA;
        return result;
    }
    if ( output.nCount < result.cbRequired ) {
        result.status = cooked_material_status_t::OUTPUT_TOO_SMALL;
        return result;
    }
    if ( OutputOverlapsMaterial(
             output,
             result.cbRequired,
             material,
             canonical ) ) {
        result.status = cooked_material_status_t::INVALID_ARGUMENT;
        return result;
    }

    // Deterministic padding and sorted records make cooked output reproducible.
    const usize cbPrefix = CookedResource_PrefixSize( 2u );
    if ( chunks[0].iOffset > cbPrefix ) {
        Cy_MemZero(
            output.pData + cbPrefix,
            static_cast<usize>( chunks[0].iOffset - cbPrefix ) );
    }

    const byte_span_t metadataOutput{
        output.pData + chunks[0].iOffset,
        static_cast<usize>( chunks[0].cbStored )
    };
    const byte_span_t stringOutput{
        output.pData + chunks[1].iOffset,
        static_cast<usize>( chunks[1].cbStored )
    };
    if ( !WriteMetadata( canonical, metadataOutput ) ||
         !WriteStrings( canonical, stringOutput ) ) {
        result.status = cooked_material_status_t::INVALID_METADATA;
        return result;
    }
    chunks[0].contentHash = ContentHash_Data( {
        metadataOutput.pData,
        metadataOutput.nCount
    } );
    chunks[1].contentHash = ContentHash_Data( {
        stringOutput.pData,
        stringOutput.nCount
    } );

    cooked_resource_header_t header{};
    header.resourceType = CY_RENDER_MATERIAL_RESOURCE_TYPE;
    header.nResourceVersion = CY_COOKED_MATERIAL_RESOURCE_VERSION_V1;
    header.nChunks = 2u;
    header.cbFile = result.cbRequired;
    if ( ContentHash_IsValid( sourceHash ) ) {
        header.flags |= COOKED_RESOURCE_FLAG_HAS_SOURCE_HASH;
        header.sourceHash = sourceHash;
    }

    // First publish descriptors, then hash the payload area and seal the header.
    const cooked_resource_result_t layout = CookedResource_WriteLayout(
        header,
        { chunks, 2u },
        output );
    if ( !CookedResource_Succeeded( layout ) ) {
        result.status = cooked_material_status_t::RESOURCE_ERROR;
        result.resourceStatus = layout.status;
        result.iChunk = layout.iChunk;
        return result;
    }

    header.flags |= COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH;
    header.contentHash = CookedResource_ComputeContentHash( {
        output.pData,
        result.cbRequired
    } );
    const cooked_resource_result_t sealed = CookedResource_WriteLayout(
        header,
        { chunks, 2u },
        output );
    if ( !CookedResource_Succeeded( sealed ) ) {
        result.status = cooked_material_status_t::RESOURCE_ERROR;
        result.resourceStatus = sealed.status;
        result.iChunk = sealed.iChunk;
        return result;
    }

    result.cbWritten = result.cbRequired;
    return result;
}

static cooked_material_result_t ReadMaterialV1(
    binary_block_t input,
    cooked_material_view_t *pMaterialOut ) noexcept
{
    cooked_material_result_t result{};
    if ( !BinaryBlock_IsValid( input ) || pMaterialOut == nullptr ||
         Cy_MemRangesOverlap(
             input.pData,
             input.cbSize,
             pMaterialOut,
             sizeof( *pMaterialOut ) ) ) {
        result.status = cooked_material_status_t::INVALID_ARGUMENT;
        return result;
    }

    // CYRS validation establishes all chunk bounds before material parsing begins.
    cooked_resource_header_t header{};
    cooked_chunk_desc_t chunks[2]{};
    const cooked_resource_result_t layout = CookedResource_ReadLayout(
        input,
        &header,
        { chunks, 2u } );
    if ( !CookedResource_Succeeded( layout ) ) {
        result.status = cooked_material_status_t::RESOURCE_ERROR;
        result.resourceStatus = layout.status;
        result.iChunk = layout.iChunk;
        return result;
    }
    if ( header.resourceType != CY_RENDER_MATERIAL_RESOURCE_TYPE ) {
        result.status = cooked_material_status_t::INVALID_RESOURCE_TYPE;
        return result;
    }
    if ( header.nResourceVersion != CY_COOKED_MATERIAL_RESOURCE_VERSION_V1 ) {
        result.status = cooked_material_status_t::VERSION_MISMATCH;
        return result;
    }
    if ( header.nChunks != 2u ) {
        result.status = cooked_material_status_t::INVALID_CHUNK_COUNT;
        return result;
    }
    if ( ( header.flags & COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH ) == 0u ) {
        result.status = cooked_material_status_t::INVALID_FLAGS;
        return result;
    }

    const cooked_chunk_desc_t &metadataChunk = chunks[0];
    const cooked_chunk_desc_t &stringChunk = chunks[1];
    if ( metadataChunk.chunkType != CY_COOKED_MATERIAL_METADATA_CHUNK ||
         metadataChunk.codec != cooked_chunk_codec_t::NONE ||
         metadataChunk.flags != COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ||
         metadataChunk.nAlignment !=
             CY_COOKED_MATERIAL_METADATA_ALIGNMENT ||
         metadataChunk.cbStored != metadataChunk.cbDecoded ) {
        result.status = cooked_material_status_t::INVALID_METADATA_CHUNK;
        result.iChunk = 0u;
        return result;
    }
    if ( stringChunk.chunkType != CY_COOKED_MATERIAL_STRING_CHUNK ||
         stringChunk.codec != cooked_chunk_codec_t::NONE ||
         stringChunk.flags != COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ||
         stringChunk.nAlignment != CY_COOKED_MATERIAL_STRING_ALIGNMENT ||
         stringChunk.cbStored != stringChunk.cbDecoded ||
         stringChunk.cbStored == 0u ||
         stringChunk.cbStored >
             CY_COOKED_MATERIAL_MAX_STRING_TABLE_SIZE ) {
        result.status = cooked_material_status_t::INVALID_STRING_CHUNK;
        result.iChunk = 1u;
        return result;
    }

    const binary_block_t metadataBytes{
        input.pData + metadataChunk.iOffset,
        static_cast<usize>( metadataChunk.cbStored )
    };
    const binary_block_t stringBytes{
        input.pData + stringChunk.iOffset,
        static_cast<usize>( stringChunk.cbStored )
    };
    if ( !ContentHash_Equals(
             ContentHash_Data( metadataBytes ),
             metadataChunk.contentHash ) ) {
        result.status = cooked_material_status_t::CONTENT_HASH_MISMATCH;
        result.iChunk = 0u;
        return result;
    }
    if ( !ContentHash_Equals(
             ContentHash_Data( stringBytes ),
             stringChunk.contentHash ) ) {
        result.status = cooked_material_status_t::CONTENT_HASH_MISMATCH;
        result.iChunk = 1u;
        return result;
    }

    byte_reader_t reader{};
    material_metadata_t metadata{};
    if ( !ByteReader_Init(
             &reader,
             metadataBytes,
             data_byte_order_t::LITTLE ) ||
         !ReadMetadataHeader( reader, metadata ) ) {
        result.status = cooked_material_status_t::INVALID_METADATA;
        return result;
    }
    if ( ( metadata.flags & ~CY_COOKED_MATERIAL_V1_KNOWN_FLAGS ) != 0u ) {
        result.status = cooked_material_status_t::INVALID_FLAGS;
        return result;
    }
    if ( metadata.nTextures > CY_RENDER_MATERIAL_MAX_TEXTURES ) {
        result.status = cooked_material_status_t::TEXTURE_LIMIT_EXCEEDED;
        return result;
    }
    if ( metadata.nParameters > CY_RENDER_MATERIAL_MAX_PARAMETERS ) {
        result.status = cooked_material_status_t::PARAMETER_LIMIT_EXCEEDED;
        return result;
    }
    if ( metadata.cbStringTable != stringBytes.cbSize ||
         metadataBytes.cbSize != CookedMaterial_MetadataSize(
             metadata.nTextures,
             metadata.nParameters ) ) {
        result.status = cooked_material_status_t::INVALID_METADATA;
        return result;
    }

    // Records stay on the stack because format limits are deliberately small.
    material_texture_record_t
        textureRecords[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    material_parameter_record_t
        parameterRecords[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    for ( usize iTexture = 0u;
          iTexture < metadata.nTextures;
          ++iTexture ) {
        if ( !ReadTextureRecord( reader, textureRecords[iTexture] ) ) {
            result.status = cooked_material_status_t::INVALID_METADATA;
            result.iTexture = iTexture;
            return result;
        }
    }
    for ( usize iParameter = 0u;
          iParameter < metadata.nParameters;
          ++iParameter ) {
        if ( !ReadParameterRecord( reader, parameterRecords[iParameter] ) ) {
            result.status = cooked_material_status_t::INVALID_METADATA;
            result.iParameter = iParameter;
            return result;
        }
    }
    if ( ByteReader_Remaining( &reader ) != 0u ) {
        result.status = cooked_material_status_t::INVALID_METADATA;
        return result;
    }

    // Output strings borrow MTST bytes; the complete input file must outlive the view.
    cooked_material_view_t material{};
    material.nResourceVersion = CY_COOKED_MATERIAL_RESOURCE_VERSION_V1;
    material.flags = metadata.flags;
    material.nTextures = metadata.nTextures;
    material.nParameters = metadata.nParameters;
    material.sourceHash = header.sourceHash;

    usize iString = 0u;
    if ( !ConsumeCanonicalString(
             stringBytes,
             metadata.shader,
             iString,
             material.shader ) ||
         !DataValidation_Succeeded(
             DataValidation_CheckResourcePath(
                 material.shader,
                 MaterialText( ".cyshader" ),
                 CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
        result.status = cooked_material_status_t::INVALID_SHADER_PATH;
        return result;
    }

    // Consume strings in exact writer order and reject gaps, aliases, or reordering.
    for ( usize iTexture = 0u;
          iTexture < material.nTextures;
          ++iTexture ) {
        cooked_material_texture_view_t &texture =
            material.textures[iTexture];
        if ( !ConsumeCanonicalString(
                 stringBytes,
                 textureRecords[iTexture].binding,
                 iString,
                 texture.binding ) ||
             !ConsumeCanonicalString(
                 stringBytes,
                 textureRecords[iTexture].texture,
                 iString,
                 texture.texture ) ) {
            result.status = cooked_material_status_t::INVALID_STRING;
            result.iTexture = iTexture;
            return result;
        }
        if ( !DataValidation_Succeeded(
                 DataValidation_CheckAsciiIdentifier(
                     texture.binding,
                     CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ||
             !DataValidation_Succeeded(
                 DataValidation_CheckResourcePath(
                     texture.texture,
                     MaterialText( ".cytex" ),
                     CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
            result.status = cooked_material_status_t::INVALID_TEXTURE;
            result.iTexture = iTexture;
            return result;
        }
        if ( iTexture > 0u ) {
            const i32 order = StringView_Compare(
                material.textures[iTexture - 1u].binding,
                texture.binding );
            if ( order == 0 ) {
                result.status = cooked_material_status_t::DUPLICATE_NAME;
                result.iTexture = iTexture;
                return result;
            }
            if ( order > 0 ) {
                result.status = cooked_material_status_t::NON_CANONICAL_ORDER;
                result.iTexture = iTexture;
                return result;
            }
        }
    }

    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        cooked_material_parameter_view_t &parameter =
            material.parameters[iParameter];
        if ( !ConsumeCanonicalString(
                 stringBytes,
                 parameterRecords[iParameter].name,
                 iString,
                 parameter.name ) ) {
            result.status = cooked_material_status_t::INVALID_STRING;
            result.iParameter = iParameter;
            return result;
        }
        if ( !DataValidation_Succeeded(
                 DataValidation_CheckAsciiIdentifier(
                     parameter.name,
                     CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ) {
            result.status = cooked_material_status_t::INVALID_PARAMETER;
            result.iParameter = iParameter;
            return result;
        }
        result.status = DecodeParameter(
            parameterRecords[iParameter],
            parameter );
        if ( result.status != cooked_material_status_t::OK ) {
            result.iParameter = iParameter;
            return result;
        }
        if ( iParameter > 0u ) {
            const i32 order = StringView_Compare(
                material.parameters[iParameter - 1u].name,
                parameter.name );
            if ( order == 0 ) {
                result.status = cooked_material_status_t::DUPLICATE_NAME;
                result.iParameter = iParameter;
                return result;
            }
            if ( order > 0 ) {
                result.status = cooked_material_status_t::NON_CANONICAL_ORDER;
                result.iParameter = iParameter;
                return result;
            }
        }
    }
    if ( iString != stringBytes.cbSize ) {
        result.status = cooked_material_status_t::INVALID_STRING;
        return result;
    }

    // The final layout check rejects valid-but-noncanonical chunk placement.
    usize iExpected = CookedResource_PrefixSize( 2u );
    const usize iPrefixEnd = iExpected;
    if ( !Cy_AlignUpChecked(
             iExpected,
             CY_COOKED_MATERIAL_METADATA_ALIGNMENT,
             iExpected ) ||
         metadataChunk.iOffset != iExpected ||
         !IsZeroRange( input, iPrefixEnd, iExpected ) ||
         !CheckedAdd(
             iExpected,
             static_cast<usize>( metadataChunk.cbStored ),
             iExpected ) ||
         stringChunk.iOffset != iExpected ||
         !CheckedAdd(
             iExpected,
             static_cast<usize>( stringChunk.cbStored ),
             iExpected ) ||
         iExpected != input.cbSize ) {
        result.status = cooked_material_status_t::NON_CANONICAL_LAYOUT;
        return result;
    }

    *pMaterialOut = material;
    result.cbRead = input.cbSize;
    return result;
}

namespace
{

struct material_feature_record_v2_t {
    material_string_ref_t name{};
    material_string_ref_t enumValue{};
    cooked_material_feature_value_type_t type{
        cooked_material_feature_value_type_t::BOOL
    };
    u32 bValue{ 0u };
};

struct material_texture_record_v2_t {
    material_string_ref_t binding{};
    material_string_ref_t texture{};
    material_string_ref_t sampler{};
    u64 nLogicalBinding{ 0u };
    u32 nUvSet{ 0u };
    f32 uvScale[2]{ 1.0F, 1.0F };
    f32 uvOffset[2]{ 0.0F, 0.0F };
    f32 uvRotation{ 0.0F };
};

struct material_parameter_record_v2_t {
    material_string_ref_t name{};
    u64 nLogicalBinding{ 0u };
    render_shader_value_type_t type{ render_shader_value_type_t::NONE };
    u32 iByteOffset{ 0u };
    u32 cbValue{ 0u };
    u32 cbByteSize{ 0u };
};

struct material_metadata_v2_t {
    flags32_t flags{ COOKED_MATERIAL_FLAG_NONE };
    render_material_domain_t domain{ render_material_domain_t::SURFACE };
    render_material_alpha_mode_t alphaMode{
        render_material_alpha_mode_t::OPAQUE
    };
    f64 alphaCutoff{ 0.5 };
    u32 nFeatures{ 0u };
    u32 nTextures{ 0u };
    u32 nParameters{ 0u };
    u32 cbStringTable{ 0u };
    u32 cbConstantData{ 0u };
    material_string_ref_t shader{};
    material_string_ref_t surface{};
    content_hash_t shaderInterfaceHash{};
    content_hash_t variantHash{};
    u32 iFeatures{ 0u };
    u32 iTextures{ 0u };
    u32 iParameters{ 0u };
};

struct canonical_material_v2_t {
    string_view_t shader{};
    string_view_t surface{};
    cooked_material_feature_source_v2_t
        features[CY_RENDER_MATERIAL_MAX_FEATURES]{};
    cooked_material_texture_source_v2_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    cooked_material_parameter_source_v2_t
        parameters[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    usize nFeatures{ 0u };
    usize nTextures{ 0u };
    usize nParameters{ 0u };
    usize cbStringTable{ 0u };
    usize cbConstantData{ 0u };
    flags32_t flags{ COOKED_MATERIAL_FLAG_NONE };
    render_material_domain_t domain{ render_material_domain_t::SURFACE };
    render_material_alpha_mode_t alphaMode{
        render_material_alpha_mode_t::OPAQUE
    };
    f32 alphaCutoff{ 0.5F };
    bool_t bHasSurface{ CY_FALSE };
    content_hash_t shaderInterfaceHash{};
    content_hash_t variantHash{};
};

CYPHER_NODISCARD f32 CanonicalFloat( f32 value ) noexcept
{
    return value == 0.0F ? 0.0F : value;
}

CYPHER_NODISCARD bool_t IsCanonicalFloat( f32 value ) noexcept
{
    return std::isfinite( value ) &&
           !( value == 0.0F && std::signbit( value ) );
}

CYPHER_NODISCARD bool_t IsCanonicalDouble( f64 value ) noexcept
{
    return std::isfinite( value ) &&
           !( value == 0.0 && std::signbit( value ) );
}

CYPHER_NODISCARD bool_t IsDomainValid(
    render_material_domain_t domain ) noexcept
{
    return domain == render_material_domain_t::SURFACE ||
           domain == render_material_domain_t::DECAL ||
           domain == render_material_domain_t::UI ||
           domain == render_material_domain_t::POSTPROCESS ||
           domain == render_material_domain_t::PARTICLE;
}

CYPHER_NODISCARD bool_t IsAlphaModeValid(
    render_material_alpha_mode_t alphaMode ) noexcept
{
    return alphaMode == render_material_alpha_mode_t::OPAQUE ||
           alphaMode == render_material_alpha_mode_t::MASK ||
           alphaMode == render_material_alpha_mode_t::BLEND ||
           alphaMode == render_material_alpha_mode_t::ADDITIVE;
}

CYPHER_NODISCARD bool_t IsFeatureTypeValid(
    cooked_material_feature_value_type_t type ) noexcept
{
    return type == cooked_material_feature_value_type_t::BOOL ||
           type == cooked_material_feature_value_type_t::ENUM;
}

CYPHER_NODISCARD usize ValueComponentCount(
    render_shader_value_type_t type ) noexcept
{
    switch ( type ) {
        case render_shader_value_type_t::BOOL:
        case render_shader_value_type_t::I32:
        case render_shader_value_type_t::U32:
        case render_shader_value_type_t::F32:
        case render_shader_value_type_t::F64: return 1u;
        case render_shader_value_type_t::I32X2:
        case render_shader_value_type_t::U32X2:
        case render_shader_value_type_t::F32X2: return 2u;
        case render_shader_value_type_t::I32X3:
        case render_shader_value_type_t::U32X3:
        case render_shader_value_type_t::F32X3: return 3u;
        case render_shader_value_type_t::I32X4:
        case render_shader_value_type_t::U32X4:
        case render_shader_value_type_t::F32X4: return 4u;
        case render_shader_value_type_t::F32X3X3: return 9u;
        case render_shader_value_type_t::F32X4X4: return 16u;
        case render_shader_value_type_t::NONE: return 0u;
    }
    return 0u;
}

CYPHER_NODISCARD bool_t IsSignedType(
    render_shader_value_type_t type ) noexcept
{
    return type == render_shader_value_type_t::I32 ||
           type == render_shader_value_type_t::I32X2 ||
           type == render_shader_value_type_t::I32X3 ||
           type == render_shader_value_type_t::I32X4;
}

CYPHER_NODISCARD bool_t IsUnsignedType(
    render_shader_value_type_t type ) noexcept
{
    return type == render_shader_value_type_t::U32 ||
           type == render_shader_value_type_t::U32X2 ||
           type == render_shader_value_type_t::U32X3 ||
           type == render_shader_value_type_t::U32X4;
}

CYPHER_NODISCARD bool_t IsFloatingType(
    render_shader_value_type_t type ) noexcept
{
    return type == render_shader_value_type_t::F32 ||
           type == render_shader_value_type_t::F64 ||
           type == render_shader_value_type_t::F32X2 ||
           type == render_shader_value_type_t::F32X3 ||
           type == render_shader_value_type_t::F32X4 ||
           type == render_shader_value_type_t::F32X3X3 ||
           type == render_shader_value_type_t::F32X4X4;
}

template <typename entry_t>
void SortNamedV2(
    entry_t *pEntries,
    usize nEntries ) noexcept
{
    for ( usize iEntry = 1u; iEntry < nEntries; ++iEntry ) {
        const entry_t value = pEntries[iEntry];
        usize iInsert = iEntry;
        while ( iInsert > 0u &&
                StringView_Compare(
                    value.name,
                    pEntries[iInsert - 1u].name ) < 0 ) {
            pEntries[iInsert] = pEntries[iInsert - 1u];
            --iInsert;
        }
        pEntries[iInsert] = value;
    }
}

void SortTexturesV2(
    cooked_material_texture_source_v2_t *pTextures,
    usize nTextures ) noexcept
{
    for ( usize iTexture = 1u; iTexture < nTextures; ++iTexture ) {
        const cooked_material_texture_source_v2_t value = pTextures[iTexture];
        usize iInsert = iTexture;
        while ( iInsert > 0u &&
                StringView_Compare(
                    value.binding,
                    pTextures[iInsert - 1u].binding ) < 0 ) {
            pTextures[iInsert] = pTextures[iInsert - 1u];
            --iInsert;
        }
        pTextures[iInsert] = value;
    }
}

CYPHER_NODISCARD bool_t HashCanonicalFeaturesV2(
    const cooked_material_feature_source_v2_t *pFeatures,
    usize nFeatures,
    content_hash_t &hashOut ) noexcept
{
    constexpr usize cbHashBuffer = 4u +
        CY_RENDER_MATERIAL_MAX_FEATURES *
        ( 16u + 2u * CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH );
    byte bytes[cbHashBuffer]{};
    byte_writer_t writer{};
    if ( !ByteWriter_Init(
             &writer,
             Span_FromArray( bytes ),
             data_byte_order_t::LITTLE ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( nFeatures ) ) ) {
        return CY_FALSE;
    }
    for ( usize iFeature = 0u; iFeature < nFeatures; ++iFeature ) {
        const cooked_material_feature_source_v2_t &feature =
            pFeatures[iFeature];
        const u32 cchEnum = feature.type ==
                cooked_material_feature_value_type_t::ENUM
            ? static_cast<u32>( feature.enumValue.cchLength )
            : 0u;
        if ( !ByteWriter_WriteU32(
                 &writer,
                 static_cast<u32>( feature.name.cchLength ) ) ||
             !ByteWriter_Write(
                 &writer,
                 feature.name.pData,
                 feature.name.cchLength ) ||
             !ByteWriter_WriteU32(
                 &writer,
                 static_cast<u32>( feature.type ) ) ||
             !ByteWriter_WriteU32(
                 &writer,
                 feature.type == cooked_material_feature_value_type_t::BOOL &&
                         feature.bValue
                     ? 1u
                     : 0u ) ||
             !ByteWriter_WriteU32( &writer, cchEnum ) ||
             ( cchEnum != 0u &&
               !ByteWriter_Write(
                   &writer,
                   feature.enumValue.pData,
                   feature.enumValue.cchLength ) ) ) {
            return CY_FALSE;
        }
    }
    hashOut = ContentHash_Data( ByteWriter_Block( &writer ) );
    return ContentHash_IsValid( hashOut );
}

CYPHER_NODISCARD cooked_material_status_t ValidateParameterV2(
    const cooked_material_parameter_source_v2_t &parameter,
    usize &cbConstantData ) noexcept
{
    u64 expectedId = 0u;
    const u32 cbValue = CookedShader_ValueTypeValueSize( parameter.type );
    const u32 cbStorage = CookedShader_ValueTypeStorageSize( parameter.type );
    const u32 nAlignment =
        CookedShader_ValueTypeStorageAlignment( parameter.type );
    if ( !DataValidation_Succeeded(
             DataValidation_CheckAsciiIdentifier(
                 parameter.name,
                 CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ||
         !CookedShader_MakeLogicalBindingId(
             parameter.name,
             &expectedId ) ||
         parameter.nLogicalBinding != expectedId ||
         cbValue == 0u || cbStorage == 0u || nAlignment == 0u ||
         parameter.cbByteSize != cbStorage ||
         ( parameter.iByteOffset & ( nAlignment - 1u ) ) != 0u ||
         parameter.iByteOffset > CY_COOKED_MATERIAL_MAX_CONSTANT_DATA_SIZE ||
         parameter.cbByteSize >
             CY_COOKED_MATERIAL_MAX_CONSTANT_DATA_SIZE -
                 parameter.iByteOffset ) {
        return cooked_material_status_t::INVALID_PARAMETER;
    }

    if ( IsFloatingType( parameter.type ) ) {
        const usize nValues = ValueComponentCount( parameter.type );
        for ( usize iValue = 0u; iValue < nValues; ++iValue ) {
            const f64 source = parameter.floatingValues[iValue];
            if ( !std::isfinite( source ) ) {
                return cooked_material_status_t::NON_FINITE_VALUE;
            }
            if ( parameter.type != render_shader_value_type_t::F64 &&
                 !std::isfinite( static_cast<f32>( source ) ) ) {
                return cooked_material_status_t::NON_FINITE_VALUE;
            }
        }
    }

    const usize iEnd = static_cast<usize>( parameter.iByteOffset ) +
        parameter.cbByteSize;
    if ( iEnd > cbConstantData ) {
        cbConstantData = iEnd;
    }
    return cooked_material_status_t::OK;
}

CYPHER_NODISCARD cooked_material_status_t CanonicalizeMaterialV2(
    const cooked_material_source_v2_t &material,
    canonical_material_v2_t &canonical,
    cooked_material_result_t *pResult ) noexcept
{
    if ( !Span_IsValid( material.features ) ||
         !Span_IsValid( material.textures ) ||
         !Span_IsValid( material.parameters ) ) {
        return cooked_material_status_t::INVALID_ARGUMENT;
    }
    if ( material.features.nCount > CY_RENDER_MATERIAL_MAX_FEATURES ) {
        return cooked_material_status_t::FEATURE_LIMIT_EXCEEDED;
    }
    if ( material.textures.nCount > CY_RENDER_MATERIAL_MAX_TEXTURES ) {
        return cooked_material_status_t::TEXTURE_LIMIT_EXCEEDED;
    }
    if ( material.parameters.nCount > CY_RENDER_MATERIAL_MAX_PARAMETERS ) {
        return cooked_material_status_t::PARAMETER_LIMIT_EXCEEDED;
    }
    if ( ( material.flags & ~CY_COOKED_MATERIAL_V2_KNOWN_FLAGS ) != 0u ) {
        return cooked_material_status_t::INVALID_FLAGS;
    }
    if ( !IsDomainValid( material.domain ) ) {
        return cooked_material_status_t::INVALID_DOMAIN;
    }
    if ( !IsAlphaModeValid( material.alphaMode ) ) {
        return cooked_material_status_t::INVALID_ALPHA_MODE;
    }
    if ( !std::isfinite( material.alphaCutoff ) ||
         material.alphaCutoff < 0.0 || material.alphaCutoff > 1.0 ||
         !std::isfinite( static_cast<f32>( material.alphaCutoff ) ) ) {
        return cooked_material_status_t::NON_FINITE_VALUE;
    }
    if ( !ContentHash_IsValid( material.shaderInterfaceHash ) ) {
        return cooked_material_status_t::INVALID_INTERFACE_HASH;
    }
    if ( !DataValidation_Succeeded(
             DataValidation_CheckResourcePath(
                 material.shader,
                 MaterialText( ".cyshader" ),
                 CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
        return cooked_material_status_t::INVALID_SHADER_PATH;
    }
    if ( material.bHasSurface &&
         !DataValidation_Succeeded(
             DataValidation_CheckResourcePath(
                 material.surface,
                 MaterialText( ".cysurface" ),
                 CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
        return cooked_material_status_t::INVALID_SURFACE_PATH;
    }
    if ( !material.bHasSurface &&
         ( !StringView_IsValid( material.surface ) ||
           material.surface.cchLength != 0u ) ) {
        return cooked_material_status_t::INVALID_SURFACE_PATH;
    }

    canonical.shader = material.shader;
    canonical.surface = material.surface;
    canonical.nFeatures = material.features.nCount;
    canonical.nTextures = material.textures.nCount;
    canonical.nParameters = material.parameters.nCount;
    canonical.flags = material.flags;
    canonical.domain = material.domain;
    canonical.alphaMode = material.alphaMode;
    canonical.alphaCutoff = material.alphaMode ==
            render_material_alpha_mode_t::MASK
        ? CanonicalFloat( static_cast<f32>( material.alphaCutoff ) )
        : 0.5F;
    canonical.bHasSurface = material.bHasSurface;
    canonical.shaderInterfaceHash = material.shaderInterfaceHash;

    usize cbStrings = 0u;
    if ( !AddStringSize( canonical.shader, cbStrings ) ||
         ( canonical.bHasSurface &&
           !AddStringSize( canonical.surface, cbStrings ) ) ) {
        return cooked_material_status_t::INVALID_STRING;
    }

    for ( usize iFeature = 0u;
          iFeature < canonical.nFeatures;
          ++iFeature ) {
        const cooked_material_feature_source_v2_t &feature =
            material.features.pData[iFeature];
        if ( !DataValidation_Succeeded(
                 DataValidation_CheckAsciiIdentifier(
                     feature.name,
                     CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ||
             !IsFeatureTypeValid( feature.type ) ||
             ( feature.type == cooked_material_feature_value_type_t::ENUM &&
               !DataValidation_Succeeded(
                   DataValidation_CheckAsciiIdentifier(
                       feature.enumValue,
                       CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ) ||
             ( feature.type == cooked_material_feature_value_type_t::BOOL &&
               ( !StringView_IsValid( feature.enumValue ) ||
                 feature.enumValue.cchLength != 0u ) ) ) {
            if ( pResult != nullptr ) {
                pResult->iFeature = iFeature;
            }
            return cooked_material_status_t::INVALID_FEATURE;
        }
        if ( !AddStringSize( feature.name, cbStrings ) ||
             ( feature.type == cooked_material_feature_value_type_t::ENUM &&
               !AddStringSize( feature.enumValue, cbStrings ) ) ) {
            if ( pResult != nullptr ) {
                pResult->iFeature = iFeature;
            }
            return cooked_material_status_t::INVALID_STRING;
        }
        canonical.features[iFeature] = feature;
    }

    for ( usize iTexture = 0u;
          iTexture < canonical.nTextures;
          ++iTexture ) {
        const cooked_material_texture_source_v2_t &texture =
            material.textures.pData[iTexture];
        u64 expectedId = 0u;
        if ( !DataValidation_Succeeded(
                 DataValidation_CheckAsciiIdentifier(
                     texture.binding,
                     CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ||
             !DataValidation_Succeeded(
                 DataValidation_CheckResourcePath(
                     texture.texture,
                     MaterialText( ".cytex" ),
                     CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ||
             !CookedShader_MakeLogicalBindingId(
                 texture.binding,
                 &expectedId ) ||
             texture.nLogicalBinding != expectedId ||
             texture.nUvSet > CY_RENDER_MATERIAL_MAX_UV_SET ||
             ( texture.bHasSampler &&
               !DataValidation_Succeeded(
                   DataValidation_CheckAsciiIdentifier(
                       texture.sampler,
                       CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ) ||
             ( !texture.bHasSampler &&
               ( !StringView_IsValid( texture.sampler ) ||
                 texture.sampler.cchLength != 0u ) ) ) {
            if ( pResult != nullptr ) {
                pResult->iTexture = iTexture;
            }
            return cooked_material_status_t::INVALID_TEXTURE;
        }
        const f64 values[5]{
            texture.uvScale[0], texture.uvScale[1],
            texture.uvOffset[0], texture.uvOffset[1], texture.uvRotation
        };
        for ( f64 value : values ) {
            if ( !std::isfinite( value ) ||
                 !std::isfinite( static_cast<f32>( value ) ) ) {
                if ( pResult != nullptr ) {
                    pResult->iTexture = iTexture;
                }
                return cooked_material_status_t::NON_FINITE_VALUE;
            }
        }
        if ( !AddStringSize( texture.binding, cbStrings ) ||
             !AddStringSize( texture.texture, cbStrings ) ||
             ( texture.bHasSampler &&
               !AddStringSize( texture.sampler, cbStrings ) ) ) {
            if ( pResult != nullptr ) {
                pResult->iTexture = iTexture;
            }
            return cooked_material_status_t::INVALID_STRING;
        }
        canonical.textures[iTexture] = texture;
    }

    for ( usize iParameter = 0u;
          iParameter < canonical.nParameters;
          ++iParameter ) {
        const cooked_material_parameter_source_v2_t &parameter =
            material.parameters.pData[iParameter];
        const cooked_material_status_t status = ValidateParameterV2(
            parameter,
            canonical.cbConstantData );
        if ( status != cooked_material_status_t::OK ) {
            if ( pResult != nullptr ) {
                pResult->iParameter = iParameter;
            }
            return status;
        }
        if ( !AddStringSize( parameter.name, cbStrings ) ) {
            if ( pResult != nullptr ) {
                pResult->iParameter = iParameter;
            }
            return cooked_material_status_t::INVALID_STRING;
        }
        canonical.parameters[iParameter] = parameter;
    }

    SortNamedV2( canonical.features, canonical.nFeatures );
    SortTexturesV2( canonical.textures, canonical.nTextures );
    SortNamedV2( canonical.parameters, canonical.nParameters );

    for ( usize iFeature = 1u;
          iFeature < canonical.nFeatures;
          ++iFeature ) {
        if ( StringView_Equals(
                 canonical.features[iFeature - 1u].name,
                 canonical.features[iFeature].name ) ) {
            if ( pResult != nullptr ) {
                pResult->iFeature = iFeature;
            }
            return cooked_material_status_t::DUPLICATE_NAME;
        }
    }
    for ( usize iTexture = 1u;
          iTexture < canonical.nTextures;
          ++iTexture ) {
        if ( StringView_Equals(
                 canonical.textures[iTexture - 1u].binding,
                 canonical.textures[iTexture].binding ) ) {
            if ( pResult != nullptr ) {
                pResult->iTexture = iTexture;
            }
            return cooked_material_status_t::DUPLICATE_NAME;
        }
    }
    for ( usize iParameter = 1u;
          iParameter < canonical.nParameters;
          ++iParameter ) {
        if ( StringView_Equals(
                 canonical.parameters[iParameter - 1u].name,
                 canonical.parameters[iParameter].name ) ) {
            if ( pResult != nullptr ) {
                pResult->iParameter = iParameter;
            }
            return cooked_material_status_t::DUPLICATE_NAME;
        }
    }

    usize iExpectedConstant = 0u;
    for ( usize iParameter = 0u;
          iParameter < canonical.nParameters;
          ++iParameter ) {
        const cooked_material_parameter_source_v2_t &parameter =
            canonical.parameters[iParameter];
        usize iAlignedOffset = 0u;
        if ( !Cy_AlignUpChecked(
                 iExpectedConstant,
                 CookedShader_ValueTypeStorageAlignment( parameter.type ),
                 iAlignedOffset ) ||
             parameter.iByteOffset != iAlignedOffset ) {
            if ( pResult != nullptr ) {
                pResult->iParameter = iParameter;
            }
            return cooked_material_status_t::INVALID_CONSTANT_DATA;
        }
        iExpectedConstant = iAlignedOffset + parameter.cbByteSize;
    }
    canonical.cbConstantData = iExpectedConstant;

    for ( usize iTexture = 0u;
          iTexture < canonical.nTextures;
          ++iTexture ) {
        for ( usize iOther = iTexture + 1u;
              iOther < canonical.nTextures;
              ++iOther ) {
            if ( canonical.textures[iTexture].nLogicalBinding ==
                 canonical.textures[iOther].nLogicalBinding ) {
                if ( pResult != nullptr ) {
                    pResult->iTexture = iOther;
                }
                return cooked_material_status_t::DUPLICATE_BINDING_ID;
            }
        }
        for ( usize iParameter = 0u;
              iParameter < canonical.nParameters;
              ++iParameter ) {
            if ( canonical.textures[iTexture].nLogicalBinding ==
                 canonical.parameters[iParameter].nLogicalBinding ) {
                if ( pResult != nullptr ) {
                    pResult->iParameter = iParameter;
                }
                return cooked_material_status_t::DUPLICATE_BINDING_ID;
            }
        }
    }
    for ( usize iParameter = 0u;
          iParameter < canonical.nParameters;
          ++iParameter ) {
        const usize iBegin = canonical.parameters[iParameter].iByteOffset;
        const usize iEnd = iBegin +
            canonical.parameters[iParameter].cbByteSize;
        for ( usize iOther = iParameter + 1u;
              iOther < canonical.nParameters;
              ++iOther ) {
            if ( canonical.parameters[iParameter].nLogicalBinding ==
                 canonical.parameters[iOther].nLogicalBinding ) {
                if ( pResult != nullptr ) {
                    pResult->iParameter = iOther;
                }
                return cooked_material_status_t::DUPLICATE_BINDING_ID;
            }
            const usize iOtherBegin =
                canonical.parameters[iOther].iByteOffset;
            const usize iOtherEnd = iOtherBegin +
                canonical.parameters[iOther].cbByteSize;
            if ( iBegin < iOtherEnd && iOtherBegin < iEnd ) {
                if ( pResult != nullptr ) {
                    pResult->iParameter = iOther;
                }
                return cooked_material_status_t::INVALID_CONSTANT_DATA;
            }
        }
    }

    if ( !HashCanonicalFeaturesV2(
             canonical.features,
             canonical.nFeatures,
             canonical.variantHash ) ) {
        return cooked_material_status_t::INVALID_VARIANT_HASH;
    }
    if ( ContentHash_IsValid( material.variantHash ) &&
         !ContentHash_Equals(
             material.variantHash,
             canonical.variantHash ) ) {
        return cooked_material_status_t::INVALID_VARIANT_HASH;
    }
    canonical.cbStringTable = cbStrings;
    return cooked_material_status_t::OK;
}

CYPHER_NODISCARD bool_t PrepareCanonicalLayoutV2(
    const canonical_material_v2_t &material,
    cooked_chunk_desc_t ( &chunks )[3],
    u32 &nChunksOut,
    usize &cbFileOut ) noexcept
{
    nChunksOut = material.cbConstantData == 0u ? 2u : 3u;
    usize iOffset = CookedResource_PrefixSize( nChunksOut );
    if ( iOffset == 0u ||
         !Cy_AlignUpChecked(
             iOffset,
             CY_COOKED_MATERIAL_METADATA_ALIGNMENT,
             iOffset ) ) {
        return CY_FALSE;
    }

    const usize cbMetadata = CookedMaterial_MetadataSizeV2(
        static_cast<u32>( material.nFeatures ),
        static_cast<u32>( material.nTextures ),
        static_cast<u32>( material.nParameters ) );
    if ( cbMetadata == 0u ) {
        return CY_FALSE;
    }
    chunks[0].chunkType = CY_COOKED_MATERIAL_METADATA_CHUNK;
    chunks[0].nAlignment = CY_COOKED_MATERIAL_METADATA_ALIGNMENT;
    chunks[0].iOffset = iOffset;
    chunks[0].cbStored = cbMetadata;
    chunks[0].cbDecoded = cbMetadata;
    chunks[0].flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    if ( !CheckedAdd( iOffset, cbMetadata, iOffset ) ) {
        return CY_FALSE;
    }

    usize iStringChunk = 1u;
    if ( material.cbConstantData != 0u ) {
        if ( !Cy_AlignUpChecked(
                 iOffset,
                 CY_COOKED_MATERIAL_CONSTANT_ALIGNMENT,
                 iOffset ) ) {
            return CY_FALSE;
        }
        chunks[1].chunkType = CY_COOKED_MATERIAL_CONSTANT_CHUNK;
        chunks[1].nAlignment = CY_COOKED_MATERIAL_CONSTANT_ALIGNMENT;
        chunks[1].iOffset = iOffset;
        chunks[1].cbStored = material.cbConstantData;
        chunks[1].cbDecoded = material.cbConstantData;
        chunks[1].flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
        if ( !CheckedAdd( iOffset, material.cbConstantData, iOffset ) ) {
            return CY_FALSE;
        }
        iStringChunk = 2u;
    }

    chunks[iStringChunk].chunkType = CY_COOKED_MATERIAL_STRING_CHUNK;
    chunks[iStringChunk].nAlignment = CY_COOKED_MATERIAL_STRING_ALIGNMENT;
    chunks[iStringChunk].iOffset = iOffset;
    chunks[iStringChunk].cbStored = material.cbStringTable;
    chunks[iStringChunk].cbDecoded = material.cbStringTable;
    chunks[iStringChunk].flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    return CheckedAdd( iOffset, material.cbStringTable, cbFileOut );
}

CYPHER_NODISCARD bool_t WriteMetadataV2(
    const canonical_material_v2_t &material,
    byte_span_t output ) noexcept
{
    material_feature_record_v2_t
        features[CY_RENDER_MATERIAL_MAX_FEATURES]{};
    material_texture_record_v2_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    material_parameter_record_v2_t
        parameters[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};

    usize iString = 0u;
    const material_string_ref_t shader =
        NextStringRef( material.shader, iString );
    material_string_ref_t surface{};
    if ( material.bHasSurface ) {
        surface = NextStringRef( material.surface, iString );
    }
    for ( usize iFeature = 0u;
          iFeature < material.nFeatures;
          ++iFeature ) {
        const cooked_material_feature_source_v2_t &source =
            material.features[iFeature];
        features[iFeature].name = NextStringRef( source.name, iString );
        features[iFeature].type = source.type;
        features[iFeature].bValue =
            source.type == cooked_material_feature_value_type_t::BOOL &&
                    source.bValue
                ? 1u
                : 0u;
        if ( source.type == cooked_material_feature_value_type_t::ENUM ) {
            features[iFeature].enumValue = NextStringRef(
                source.enumValue,
                iString );
        }
    }
    for ( usize iTexture = 0u;
          iTexture < material.nTextures;
          ++iTexture ) {
        const cooked_material_texture_source_v2_t &source =
            material.textures[iTexture];
        material_texture_record_v2_t &record = textures[iTexture];
        record.binding = NextStringRef( source.binding, iString );
        record.texture = NextStringRef( source.texture, iString );
        if ( source.bHasSampler ) {
            record.sampler = NextStringRef( source.sampler, iString );
        }
        record.nLogicalBinding = source.nLogicalBinding;
        record.nUvSet = source.nUvSet;
        record.uvScale[0] = CanonicalFloat(
            static_cast<f32>( source.uvScale[0] ) );
        record.uvScale[1] = CanonicalFloat(
            static_cast<f32>( source.uvScale[1] ) );
        record.uvOffset[0] = CanonicalFloat(
            static_cast<f32>( source.uvOffset[0] ) );
        record.uvOffset[1] = CanonicalFloat(
            static_cast<f32>( source.uvOffset[1] ) );
        record.uvRotation = CanonicalFloat(
            static_cast<f32>( source.uvRotation ) );
    }
    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        const cooked_material_parameter_source_v2_t &source =
            material.parameters[iParameter];
        material_parameter_record_v2_t &record = parameters[iParameter];
        record.name = NextStringRef( source.name, iString );
        record.nLogicalBinding = source.nLogicalBinding;
        record.type = source.type;
        record.iByteOffset = source.iByteOffset;
        record.cbValue = CookedShader_ValueTypeValueSize( source.type );
        record.cbByteSize = source.cbByteSize;
    }
    if ( iString != material.cbStringTable ) {
        return CY_FALSE;
    }

    const u32 iFeatures =
        static_cast<u32>( CY_COOKED_MATERIAL_METADATA_HEADER_SIZE_V2 );
    const u32 iTextures = iFeatures +
        static_cast<u32>( material.nFeatures *
                          CY_COOKED_MATERIAL_FEATURE_RECORD_SIZE_V2 );
    const u32 iParameters = iTextures +
        static_cast<u32>( material.nTextures *
                          CY_COOKED_MATERIAL_TEXTURE_RECORD_SIZE_V2 );

    byte_writer_t writer{};
    if ( !ByteWriter_Init(
             &writer,
             output,
             data_byte_order_t::LITTLE ) ||
         !ByteWriter_WriteU32( &writer, CY_COOKED_MATERIAL_METADATA_MAGIC ) ||
         !ByteWriter_WriteU32(
             &writer,
             CY_COOKED_MATERIAL_METADATA_VERSION_V2 ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>(
                 CY_COOKED_MATERIAL_METADATA_HEADER_SIZE_V2 ) ) ||
         !ByteWriter_WriteU32( &writer, material.flags ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( material.domain ) ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( material.alphaMode ) ) ||
         !ByteWriter_WriteF64(
             &writer,
             static_cast<f64>( material.alphaCutoff ) ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( material.nFeatures ) ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( material.nTextures ) ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( material.nParameters ) ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( material.cbStringTable ) ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( material.cbConstantData ) ) ||
         !ByteWriter_WriteU32( &writer, 0u ) ||
         !WriteStringRef( writer, shader ) ||
         !WriteStringRef( writer, surface ) ||
         !ByteWriter_WriteU64(
             &writer,
             material.shaderInterfaceHash.low ) ||
         !ByteWriter_WriteU64(
             &writer,
             material.shaderInterfaceHash.high ) ||
         !ByteWriter_WriteU64( &writer, material.variantHash.low ) ||
         !ByteWriter_WriteU64( &writer, material.variantHash.high ) ||
         !ByteWriter_WriteU32( &writer, iFeatures ) ||
         !ByteWriter_WriteU32( &writer, iTextures ) ||
         !ByteWriter_WriteU32( &writer, iParameters ) ||
         !ByteWriter_WriteU32( &writer, 0u ) ||
         !ByteWriter_WriteU64( &writer, 0u ) ) {
        return CY_FALSE;
    }

    for ( usize iFeature = 0u;
          iFeature < material.nFeatures;
          ++iFeature ) {
        const material_feature_record_v2_t &record = features[iFeature];
        if ( !WriteStringRef( writer, record.name ) ||
             !WriteStringRef( writer, record.enumValue ) ||
             !ByteWriter_WriteU32(
                 &writer,
                 static_cast<u32>( record.type ) ) ||
             !ByteWriter_WriteU32( &writer, record.bValue ) ||
             !ByteWriter_WriteU64( &writer, 0u ) ) {
            return CY_FALSE;
        }
    }
    for ( usize iTexture = 0u;
          iTexture < material.nTextures;
          ++iTexture ) {
        const material_texture_record_v2_t &record = textures[iTexture];
        if ( !WriteStringRef( writer, record.binding ) ||
             !WriteStringRef( writer, record.texture ) ||
             !WriteStringRef( writer, record.sampler ) ||
             !ByteWriter_WriteU64(
                 &writer,
                 record.nLogicalBinding ) ||
             !ByteWriter_WriteU32( &writer, record.nUvSet ) ||
             !ByteWriter_WriteU32( &writer, 0u ) ||
             !ByteWriter_WriteF32( &writer, record.uvScale[0] ) ||
             !ByteWriter_WriteF32( &writer, record.uvScale[1] ) ||
             !ByteWriter_WriteF32( &writer, record.uvOffset[0] ) ||
             !ByteWriter_WriteF32( &writer, record.uvOffset[1] ) ||
             !ByteWriter_WriteF32( &writer, record.uvRotation ) ||
             !ByteWriter_WriteU32( &writer, 0u ) ) {
            return CY_FALSE;
        }
    }
    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        const material_parameter_record_v2_t &record = parameters[iParameter];
        if ( !WriteStringRef( writer, record.name ) ||
             !ByteWriter_WriteU64(
                 &writer,
                 record.nLogicalBinding ) ||
             !ByteWriter_WriteU32(
                 &writer,
                 static_cast<u32>( record.type ) ) ||
             !ByteWriter_WriteU32( &writer, record.iByteOffset ) ||
             !ByteWriter_WriteU32( &writer, record.cbValue ) ||
             !ByteWriter_WriteU32( &writer, record.cbByteSize ) ||
             !ByteWriter_WriteU64( &writer, 0u ) ) {
            return CY_FALSE;
        }
    }
    return ByteWriter_BytesWritten( &writer ) == output.nCount;
}

CYPHER_NODISCARD bool_t WriteStringsV2(
    const canonical_material_v2_t &material,
    byte_span_t output ) noexcept
{
    byte_writer_t writer{};
    if ( !ByteWriter_Init(
             &writer,
             output,
             data_byte_order_t::LITTLE ) ||
         !ByteWriter_WriteString( &writer, material.shader, CY_TRUE ) ||
         ( material.bHasSurface &&
           !ByteWriter_WriteString( &writer, material.surface, CY_TRUE ) ) ) {
        return CY_FALSE;
    }
    for ( usize iFeature = 0u;
          iFeature < material.nFeatures;
          ++iFeature ) {
        const cooked_material_feature_source_v2_t &feature =
            material.features[iFeature];
        if ( !ByteWriter_WriteString( &writer, feature.name, CY_TRUE ) ||
             ( feature.type == cooked_material_feature_value_type_t::ENUM &&
               !ByteWriter_WriteString(
                   &writer,
                   feature.enumValue,
                   CY_TRUE ) ) ) {
            return CY_FALSE;
        }
    }
    for ( usize iTexture = 0u;
          iTexture < material.nTextures;
          ++iTexture ) {
        const cooked_material_texture_source_v2_t &texture =
            material.textures[iTexture];
        if ( !ByteWriter_WriteString( &writer, texture.binding, CY_TRUE ) ||
             !ByteWriter_WriteString( &writer, texture.texture, CY_TRUE ) ||
             ( texture.bHasSampler &&
               !ByteWriter_WriteString(
                   &writer,
                   texture.sampler,
                   CY_TRUE ) ) ) {
            return CY_FALSE;
        }
    }
    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        if ( !ByteWriter_WriteString(
                 &writer,
                 material.parameters[iParameter].name,
                 CY_TRUE ) ) {
            return CY_FALSE;
        }
    }
    return ByteWriter_BytesWritten( &writer ) == output.nCount;
}

CYPHER_NODISCARD bool_t WriteParameterValueV2(
    const cooked_material_parameter_source_v2_t &parameter,
    byte_span_t output ) noexcept
{
    byte_writer_t writer{};
    if ( !ByteWriter_Init(
             &writer,
             output,
             data_byte_order_t::LITTLE ) ) {
        return CY_FALSE;
    }
    const usize nValues = ValueComponentCount( parameter.type );
    if ( parameter.type == render_shader_value_type_t::BOOL ) {
        if ( !ByteWriter_WriteU32(
                 &writer,
                 parameter.bValue ? 1u : 0u ) ) {
            return CY_FALSE;
        }
    } else if ( IsSignedType( parameter.type ) ) {
        for ( usize iValue = 0u; iValue < nValues; ++iValue ) {
            if ( !ByteWriter_WriteI32(
                     &writer,
                     parameter.signedValues[iValue] ) ) {
                return CY_FALSE;
            }
        }
    } else if ( IsUnsignedType( parameter.type ) ) {
        for ( usize iValue = 0u; iValue < nValues; ++iValue ) {
            if ( !ByteWriter_WriteU32(
                     &writer,
                     parameter.unsignedValues[iValue] ) ) {
                return CY_FALSE;
            }
        }
    } else if ( parameter.type == render_shader_value_type_t::F64 ) {
        if ( !ByteWriter_WriteF64(
                 &writer,
                 CanonicalNumber( parameter.floatingValues[0] ) ) ) {
            return CY_FALSE;
        }
    } else if ( parameter.type == render_shader_value_type_t::F32X3X3 ) {
        // Matrix values are column-major. Each three-component column occupies
        // one padded 16-byte slot so the bytes can be uploaded directly using
        // the shared CYSH/CYMT material-constant layout.
        for ( usize iColumn = 0u; iColumn < 3u; ++iColumn ) {
            for ( usize iRow = 0u; iRow < 3u; ++iRow ) {
                const usize iValue = iColumn * 3u + iRow;
                if ( !ByteWriter_WriteF32(
                         &writer,
                         CanonicalFloat( static_cast<f32>(
                             parameter.floatingValues[iValue] ) ) ) ) {
                    return CY_FALSE;
                }
            }
            if ( !ByteWriter_WriteU32( &writer, 0u ) ) {
                return CY_FALSE;
            }
        }
    } else if ( IsFloatingType( parameter.type ) ) {
        for ( usize iValue = 0u; iValue < nValues; ++iValue ) {
            if ( !ByteWriter_WriteF32(
                     &writer,
                     CanonicalFloat( static_cast<f32>(
                         parameter.floatingValues[iValue] ) ) ) ) {
                return CY_FALSE;
            }
        }
    } else {
        return CY_FALSE;
    }
    if ( !ByteWriter_WriteZero(
             &writer,
             ByteWriter_Remaining( &writer ) ) ) {
        return CY_FALSE;
    }
    return ByteWriter_BytesWritten( &writer ) == output.nCount;
}

CYPHER_NODISCARD bool_t WriteConstantsV2(
    const canonical_material_v2_t &material,
    byte_span_t output ) noexcept
{
    if ( output.nCount != material.cbConstantData ) {
        return CY_FALSE;
    }
    Cy_MemZero( output.pData, output.nCount );
    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        const cooked_material_parameter_source_v2_t &parameter =
            material.parameters[iParameter];
        if ( !WriteParameterValueV2(
                 parameter,
                 {
                     output.pData + parameter.iByteOffset,
                     parameter.cbByteSize
                 } ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t OutputOverlapsMaterialV2(
    byte_span_t output,
    usize cbRequired,
    const cooked_material_source_v2_t &source,
    const canonical_material_v2_t &material ) noexcept
{
    if ( Cy_MemRangesOverlap(
             output.pData,
             cbRequired,
             &source,
             sizeof( source ) ) ||
         Cy_MemRangesOverlap(
             output.pData,
             cbRequired,
             source.features.pData,
             source.features.nCount *
                 sizeof( cooked_material_feature_source_v2_t ) ) ||
         Cy_MemRangesOverlap(
             output.pData,
             cbRequired,
             source.textures.pData,
             source.textures.nCount *
                 sizeof( cooked_material_texture_source_v2_t ) ) ||
         Cy_MemRangesOverlap(
             output.pData,
             cbRequired,
             source.parameters.pData,
             source.parameters.nCount *
                 sizeof( cooked_material_parameter_source_v2_t ) ) ||
         Cy_MemRangesOverlap(
             output.pData,
             cbRequired,
             material.shader.pData,
             material.shader.cchLength ) ||
         ( material.bHasSurface &&
           Cy_MemRangesOverlap(
               output.pData,
               cbRequired,
               material.surface.pData,
               material.surface.cchLength ) ) ) {
        return CY_TRUE;
    }
    for ( usize iFeature = 0u;
          iFeature < material.nFeatures;
          ++iFeature ) {
        const cooked_material_feature_source_v2_t &feature =
            material.features[iFeature];
        if ( Cy_MemRangesOverlap(
                 output.pData,
                 cbRequired,
                 feature.name.pData,
                 feature.name.cchLength ) ||
             ( feature.type == cooked_material_feature_value_type_t::ENUM &&
               Cy_MemRangesOverlap(
                   output.pData,
                   cbRequired,
                   feature.enumValue.pData,
                   feature.enumValue.cchLength ) ) ) {
            return CY_TRUE;
        }
    }
    for ( usize iTexture = 0u;
          iTexture < material.nTextures;
          ++iTexture ) {
        const cooked_material_texture_source_v2_t &texture =
            material.textures[iTexture];
        if ( Cy_MemRangesOverlap(
                 output.pData,
                 cbRequired,
                 texture.binding.pData,
                 texture.binding.cchLength ) ||
             Cy_MemRangesOverlap(
                 output.pData,
                 cbRequired,
                 texture.texture.pData,
                 texture.texture.cchLength ) ||
             ( texture.bHasSampler &&
               Cy_MemRangesOverlap(
                   output.pData,
                   cbRequired,
                   texture.sampler.pData,
                   texture.sampler.cchLength ) ) ) {
            return CY_TRUE;
        }
    }
    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        if ( Cy_MemRangesOverlap(
                 output.pData,
                 cbRequired,
                 material.parameters[iParameter].name.pData,
                 material.parameters[iParameter].name.cchLength ) ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ReadMetadataHeaderV2(
    byte_reader_t &reader,
    material_metadata_v2_t &metadata ) noexcept
{
    u32 magic = 0u;
    u32 version = 0u;
    u32 cbHeader = 0u;
    u32 domain = 0u;
    u32 alphaMode = 0u;
    u32 reserved0 = 0u;
    u32 reserved1 = 0u;
    u64 reserved2 = 0u;
    if ( !ByteReader_ReadU32( &reader, &magic ) ||
         !ByteReader_ReadU32( &reader, &version ) ||
         !ByteReader_ReadU32( &reader, &cbHeader ) ||
         !ByteReader_ReadU32( &reader, &metadata.flags ) ||
         !ByteReader_ReadU32( &reader, &domain ) ||
         !ByteReader_ReadU32( &reader, &alphaMode ) ||
         !ByteReader_ReadF64( &reader, &metadata.alphaCutoff ) ||
         !ByteReader_ReadU32( &reader, &metadata.nFeatures ) ||
         !ByteReader_ReadU32( &reader, &metadata.nTextures ) ||
         !ByteReader_ReadU32( &reader, &metadata.nParameters ) ||
         !ByteReader_ReadU32( &reader, &metadata.cbStringTable ) ||
         !ByteReader_ReadU32( &reader, &metadata.cbConstantData ) ||
         !ByteReader_ReadU32( &reader, &reserved0 ) ||
         !ReadStringRef( reader, metadata.shader ) ||
         !ReadStringRef( reader, metadata.surface ) ||
         !ByteReader_ReadU64(
             &reader,
             &metadata.shaderInterfaceHash.low ) ||
         !ByteReader_ReadU64(
             &reader,
             &metadata.shaderInterfaceHash.high ) ||
         !ByteReader_ReadU64( &reader, &metadata.variantHash.low ) ||
         !ByteReader_ReadU64( &reader, &metadata.variantHash.high ) ||
         !ByteReader_ReadU32( &reader, &metadata.iFeatures ) ||
         !ByteReader_ReadU32( &reader, &metadata.iTextures ) ||
         !ByteReader_ReadU32( &reader, &metadata.iParameters ) ||
         !ByteReader_ReadU32( &reader, &reserved1 ) ||
         !ByteReader_ReadU64( &reader, &reserved2 ) ||
         magic != CY_COOKED_MATERIAL_METADATA_MAGIC ||
         version != CY_COOKED_MATERIAL_METADATA_VERSION_V2 ||
         cbHeader != CY_COOKED_MATERIAL_METADATA_HEADER_SIZE_V2 ||
         reserved0 != 0u || reserved1 != 0u || reserved2 != 0u ) {
        return CY_FALSE;
    }
    metadata.domain = static_cast<render_material_domain_t>( domain );
    metadata.alphaMode =
        static_cast<render_material_alpha_mode_t>( alphaMode );
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ReadFeatureRecordV2(
    byte_reader_t &reader,
    material_feature_record_v2_t &record ) noexcept
{
    u32 type = 0u;
    u64 reserved = 0u;
    if ( !ReadStringRef( reader, record.name ) ||
         !ReadStringRef( reader, record.enumValue ) ||
         !ByteReader_ReadU32( &reader, &type ) ||
         !ByteReader_ReadU32( &reader, &record.bValue ) ||
         !ByteReader_ReadU64( &reader, &reserved ) || reserved != 0u ) {
        return CY_FALSE;
    }
    record.type = static_cast<cooked_material_feature_value_type_t>( type );
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ReadTextureRecordV2(
    byte_reader_t &reader,
    material_texture_record_v2_t &record ) noexcept
{
    u32 reserved0 = 0u;
    u32 reserved1 = 0u;
    return ReadStringRef( reader, record.binding ) &&
           ReadStringRef( reader, record.texture ) &&
           ReadStringRef( reader, record.sampler ) &&
           ByteReader_ReadU64( &reader, &record.nLogicalBinding ) &&
           ByteReader_ReadU32( &reader, &record.nUvSet ) &&
           ByteReader_ReadU32( &reader, &reserved0 ) &&
           ByteReader_ReadF32( &reader, &record.uvScale[0] ) &&
           ByteReader_ReadF32( &reader, &record.uvScale[1] ) &&
           ByteReader_ReadF32( &reader, &record.uvOffset[0] ) &&
           ByteReader_ReadF32( &reader, &record.uvOffset[1] ) &&
           ByteReader_ReadF32( &reader, &record.uvRotation ) &&
           ByteReader_ReadU32( &reader, &reserved1 ) &&
           reserved0 == 0u && reserved1 == 0u;
}

CYPHER_NODISCARD bool_t ReadParameterRecordV2(
    byte_reader_t &reader,
    material_parameter_record_v2_t &record ) noexcept
{
    u32 type = 0u;
    u64 reserved = 0u;
    if ( !ReadStringRef( reader, record.name ) ||
         !ByteReader_ReadU64( &reader, &record.nLogicalBinding ) ||
         !ByteReader_ReadU32( &reader, &type ) ||
         !ByteReader_ReadU32( &reader, &record.iByteOffset ) ||
         !ByteReader_ReadU32( &reader, &record.cbValue ) ||
         !ByteReader_ReadU32( &reader, &record.cbByteSize ) ||
         !ByteReader_ReadU64( &reader, &reserved ) || reserved != 0u ) {
        return CY_FALSE;
    }
    record.type = static_cast<render_shader_value_type_t>( type );
    return CY_TRUE;
}

CYPHER_NODISCARD cooked_material_status_t DecodeParameterValueV2(
    const material_parameter_record_v2_t &record,
    binary_block_t constants,
    cooked_material_parameter_view_t &parameter ) noexcept
{
    if ( record.iByteOffset > constants.cbSize ||
         record.cbByteSize > constants.cbSize - record.iByteOffset ) {
        return cooked_material_status_t::INVALID_CONSTANT_DATA;
    }
    const binary_block_t storageBytes{
        constants.pData + record.iByteOffset,
        record.cbByteSize
    };
    byte_reader_t reader{};
    if ( !ByteReader_Init(
             &reader,
             storageBytes,
             data_byte_order_t::LITTLE ) ) {
        return cooked_material_status_t::INVALID_CONSTANT_DATA;
    }
    const usize nValues = ValueComponentCount( record.type );
    if ( record.type == render_shader_value_type_t::BOOL ) {
        u32 value = 0u;
        if ( !ByteReader_ReadU32( &reader, &value ) || value > 1u ) {
            return cooked_material_status_t::INVALID_CONSTANT_DATA;
        }
        parameter.bValue = value != 0u;
    } else if ( IsSignedType( record.type ) ) {
        for ( usize iValue = 0u; iValue < nValues; ++iValue ) {
            if ( !ByteReader_ReadI32(
                     &reader,
                     &parameter.signedValues[iValue] ) ) {
                return cooked_material_status_t::INVALID_CONSTANT_DATA;
            }
        }
    } else if ( IsUnsignedType( record.type ) ) {
        for ( usize iValue = 0u; iValue < nValues; ++iValue ) {
            if ( !ByteReader_ReadU32(
                     &reader,
                     &parameter.unsignedValues[iValue] ) ) {
                return cooked_material_status_t::INVALID_CONSTANT_DATA;
            }
        }
    } else if ( record.type == render_shader_value_type_t::F64 ) {
        if ( !ByteReader_ReadF64(
                 &reader,
                 &parameter.floatingValues[0] ) ||
             !IsCanonicalDouble( parameter.floatingValues[0] ) ) {
            return std::isfinite( parameter.floatingValues[0] )
                ? cooked_material_status_t::NON_CANONICAL_LAYOUT
                : cooked_material_status_t::NON_FINITE_VALUE;
        }
    } else if ( record.type == render_shader_value_type_t::F32X3X3 ) {
        for ( usize iColumn = 0u; iColumn < 3u; ++iColumn ) {
            for ( usize iRow = 0u; iRow < 3u; ++iRow ) {
                const usize iValue = iColumn * 3u + iRow;
                f32 value = 0.0F;
                if ( !ByteReader_ReadF32( &reader, &value ) ) {
                    return cooked_material_status_t::INVALID_CONSTANT_DATA;
                }
                if ( !std::isfinite( value ) ) {
                    return cooked_material_status_t::NON_FINITE_VALUE;
                }
                if ( value == 0.0F && std::signbit( value ) ) {
                    return cooked_material_status_t::NON_CANONICAL_LAYOUT;
                }
                parameter.floatingValues[iValue] = value;
            }
            u32 padding = 0u;
            if ( !ByteReader_ReadU32( &reader, &padding ) ||
                 padding != 0u ) {
                return cooked_material_status_t::NON_CANONICAL_LAYOUT;
            }
        }
    } else if ( IsFloatingType( record.type ) ) {
        for ( usize iValue = 0u; iValue < nValues; ++iValue ) {
            f32 value = 0.0F;
            if ( !ByteReader_ReadF32( &reader, &value ) ) {
                return cooked_material_status_t::INVALID_CONSTANT_DATA;
            }
            if ( !std::isfinite( value ) ) {
                return cooked_material_status_t::NON_FINITE_VALUE;
            }
            if ( value == 0.0F && std::signbit( value ) ) {
                return cooked_material_status_t::NON_CANONICAL_LAYOUT;
            }
            parameter.floatingValues[iValue] = value;
        }
    } else {
        return cooked_material_status_t::INVALID_PARAMETER;
    }
    if ( !IsZeroRange(
             storageBytes,
             ByteReader_Offset( &reader ),
             storageBytes.cbSize ) ) {
        return cooked_material_status_t::NON_CANONICAL_LAYOUT;
    }
    parameter.shaderType = record.type;
    parameter.nLogicalBinding = record.nLogicalBinding;
    parameter.iByteOffset = record.iByteOffset;
    parameter.cbValue = record.cbValue;
    parameter.cbByteSize = record.cbByteSize;
    parameter.data = storageBytes;
    return cooked_material_status_t::OK;
}

struct material_constant_range_t {
    u32 iBegin{ 0u };
    u32 iStorageEnd{ 0u };
    usize iParameter{ 0u };
};

void SortConstantRangesV2(
    material_constant_range_t *pRanges,
    usize nRanges ) noexcept
{
    for ( usize iRange = 1u; iRange < nRanges; ++iRange ) {
        const material_constant_range_t value = pRanges[iRange];
        usize iInsert = iRange;
        while ( iInsert > 0u &&
                value.iBegin < pRanges[iInsert - 1u].iBegin ) {
            pRanges[iInsert] = pRanges[iInsert - 1u];
            --iInsert;
        }
        pRanges[iInsert] = value;
    }
}

CYPHER_NODISCARD cooked_material_result_t ReadMaterialV2(
    binary_block_t input,
    cooked_material_view_t *pMaterialOut ) noexcept
{
    cooked_material_result_t result{};
    if ( !BinaryBlock_IsValid( input ) || pMaterialOut == nullptr ||
         Cy_MemRangesOverlap(
             input.pData,
             input.cbSize,
             pMaterialOut,
             sizeof( *pMaterialOut ) ) ) {
        result.status = cooked_material_status_t::INVALID_ARGUMENT;
        return result;
    }

    cooked_resource_header_t header{};
    cooked_chunk_desc_t chunks[3]{};
    const cooked_resource_result_t layout = CookedResource_ReadLayout(
        input,
        &header,
        { chunks, 3u } );
    if ( !CookedResource_Succeeded( layout ) ) {
        result.status = cooked_material_status_t::RESOURCE_ERROR;
        result.resourceStatus = layout.status;
        result.iChunk = layout.iChunk;
        return result;
    }
    if ( header.resourceType != CY_RENDER_MATERIAL_RESOURCE_TYPE ) {
        result.status = cooked_material_status_t::INVALID_RESOURCE_TYPE;
        return result;
    }
    if ( header.nResourceVersion !=
         CY_COOKED_MATERIAL_RESOURCE_VERSION_V2 ) {
        result.status = cooked_material_status_t::VERSION_MISMATCH;
        return result;
    }
    if ( header.nChunks != 2u && header.nChunks != 3u ) {
        result.status = cooked_material_status_t::INVALID_CHUNK_COUNT;
        return result;
    }
    if ( ( header.flags & COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH ) == 0u ) {
        result.status = cooked_material_status_t::INVALID_FLAGS;
        return result;
    }

    const bool_t bHasConstants = header.nChunks == 3u;
    const usize iStringChunk = header.nChunks - 1u;
    const cooked_chunk_desc_t &metadataChunk = chunks[0];
    const cooked_chunk_desc_t &stringChunk = chunks[iStringChunk];
    if ( metadataChunk.chunkType != CY_COOKED_MATERIAL_METADATA_CHUNK ||
         metadataChunk.codec != cooked_chunk_codec_t::NONE ||
         metadataChunk.flags != COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ||
         metadataChunk.nAlignment !=
             CY_COOKED_MATERIAL_METADATA_ALIGNMENT ||
         metadataChunk.cbStored != metadataChunk.cbDecoded ) {
        result.status = cooked_material_status_t::INVALID_METADATA_CHUNK;
        result.iChunk = 0u;
        return result;
    }
    if ( bHasConstants ) {
        const cooked_chunk_desc_t &constantChunk = chunks[1];
        if ( constantChunk.chunkType !=
                 CY_COOKED_MATERIAL_CONSTANT_CHUNK ||
             constantChunk.codec != cooked_chunk_codec_t::NONE ||
             constantChunk.flags != COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ||
             constantChunk.nAlignment !=
                 CY_COOKED_MATERIAL_CONSTANT_ALIGNMENT ||
             constantChunk.cbStored != constantChunk.cbDecoded ||
             constantChunk.cbStored == 0u ||
             constantChunk.cbStored >
                 CY_COOKED_MATERIAL_MAX_CONSTANT_DATA_SIZE ) {
            result.status = cooked_material_status_t::INVALID_CONSTANT_CHUNK;
            result.iChunk = 1u;
            return result;
        }
    }
    if ( stringChunk.chunkType != CY_COOKED_MATERIAL_STRING_CHUNK ||
         stringChunk.codec != cooked_chunk_codec_t::NONE ||
         stringChunk.flags != COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ||
         stringChunk.nAlignment != CY_COOKED_MATERIAL_STRING_ALIGNMENT ||
         stringChunk.cbStored != stringChunk.cbDecoded ||
         stringChunk.cbStored == 0u ||
         stringChunk.cbStored > CY_COOKED_MATERIAL_MAX_STRING_TABLE_SIZE ) {
        result.status = cooked_material_status_t::INVALID_STRING_CHUNK;
        result.iChunk = iStringChunk;
        return result;
    }

    const binary_block_t metadataBytes{
        input.pData + metadataChunk.iOffset,
        static_cast<usize>( metadataChunk.cbStored )
    };
    const binary_block_t constantBytes = bHasConstants
        ? binary_block_t{
              input.pData + chunks[1].iOffset,
              static_cast<usize>( chunks[1].cbStored )
          }
        : binary_block_t{};
    const binary_block_t stringBytes{
        input.pData + stringChunk.iOffset,
        static_cast<usize>( stringChunk.cbStored )
    };
    if ( !ContentHash_Equals(
             ContentHash_Data( metadataBytes ),
             metadataChunk.contentHash ) ) {
        result.status = cooked_material_status_t::CONTENT_HASH_MISMATCH;
        result.iChunk = 0u;
        return result;
    }
    if ( bHasConstants &&
         !ContentHash_Equals(
             ContentHash_Data( constantBytes ),
             chunks[1].contentHash ) ) {
        result.status = cooked_material_status_t::CONTENT_HASH_MISMATCH;
        result.iChunk = 1u;
        return result;
    }
    if ( !ContentHash_Equals(
             ContentHash_Data( stringBytes ),
             stringChunk.contentHash ) ) {
        result.status = cooked_material_status_t::CONTENT_HASH_MISMATCH;
        result.iChunk = iStringChunk;
        return result;
    }

    byte_reader_t reader{};
    material_metadata_v2_t metadata{};
    if ( !ByteReader_Init(
             &reader,
             metadataBytes,
             data_byte_order_t::LITTLE ) ||
         !ReadMetadataHeaderV2( reader, metadata ) ) {
        result.status = cooked_material_status_t::INVALID_METADATA;
        return result;
    }
    if ( ( metadata.flags & ~CY_COOKED_MATERIAL_V2_KNOWN_FLAGS ) != 0u ) {
        result.status = cooked_material_status_t::INVALID_FLAGS;
        return result;
    }
    if ( !IsDomainValid( metadata.domain ) ) {
        result.status = cooked_material_status_t::INVALID_DOMAIN;
        return result;
    }
    if ( !IsAlphaModeValid( metadata.alphaMode ) ) {
        result.status = cooked_material_status_t::INVALID_ALPHA_MODE;
        return result;
    }
    const f32 alphaCutoff = static_cast<f32>( metadata.alphaCutoff );
    if ( !std::isfinite( metadata.alphaCutoff ) ||
         !std::isfinite( alphaCutoff ) || alphaCutoff < 0.0F ||
         alphaCutoff > 1.0F ) {
        result.status = cooked_material_status_t::NON_FINITE_VALUE;
        return result;
    }
    if ( !IsCanonicalFloat( alphaCutoff ) ||
         static_cast<f64>( alphaCutoff ) != metadata.alphaCutoff ||
         ( metadata.alphaMode != render_material_alpha_mode_t::MASK &&
           alphaCutoff != 0.5F ) ) {
        result.status = cooked_material_status_t::NON_CANONICAL_LAYOUT;
        return result;
    }
    if ( metadata.nFeatures > CY_RENDER_MATERIAL_MAX_FEATURES ) {
        result.status = cooked_material_status_t::FEATURE_LIMIT_EXCEEDED;
        return result;
    }
    if ( metadata.nTextures > CY_RENDER_MATERIAL_MAX_TEXTURES ) {
        result.status = cooked_material_status_t::TEXTURE_LIMIT_EXCEEDED;
        return result;
    }
    if ( metadata.nParameters > CY_RENDER_MATERIAL_MAX_PARAMETERS ) {
        result.status = cooked_material_status_t::PARAMETER_LIMIT_EXCEEDED;
        return result;
    }
    const usize cbExpectedMetadata = CookedMaterial_MetadataSizeV2(
        metadata.nFeatures,
        metadata.nTextures,
        metadata.nParameters );
    const u32 iExpectedFeatures =
        static_cast<u32>( CY_COOKED_MATERIAL_METADATA_HEADER_SIZE_V2 );
    const u32 iExpectedTextures = iExpectedFeatures +
        metadata.nFeatures *
            static_cast<u32>( CY_COOKED_MATERIAL_FEATURE_RECORD_SIZE_V2 );
    const u32 iExpectedParameters = iExpectedTextures +
        metadata.nTextures *
            static_cast<u32>( CY_COOKED_MATERIAL_TEXTURE_RECORD_SIZE_V2 );
    if ( metadataBytes.cbSize != cbExpectedMetadata ||
         metadata.cbStringTable != stringBytes.cbSize ||
         metadata.cbConstantData != constantBytes.cbSize ||
         ( metadata.nParameters == 0u ) != !bHasConstants ||
         metadata.iFeatures != iExpectedFeatures ||
         metadata.iTextures != iExpectedTextures ||
         metadata.iParameters != iExpectedParameters ||
         !ContentHash_IsValid( metadata.shaderInterfaceHash ) ||
         !ContentHash_IsValid( metadata.variantHash ) ) {
        result.status = !ContentHash_IsValid( metadata.shaderInterfaceHash )
            ? cooked_material_status_t::INVALID_INTERFACE_HASH
            : !ContentHash_IsValid( metadata.variantHash )
                ? cooked_material_status_t::INVALID_VARIANT_HASH
                : cooked_material_status_t::INVALID_METADATA;
        return result;
    }

    material_feature_record_v2_t
        featureRecords[CY_RENDER_MATERIAL_MAX_FEATURES]{};
    material_texture_record_v2_t
        textureRecords[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    material_parameter_record_v2_t
        parameterRecords[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    for ( usize iFeature = 0u;
          iFeature < metadata.nFeatures;
          ++iFeature ) {
        if ( !ReadFeatureRecordV2( reader, featureRecords[iFeature] ) ) {
            result.status = cooked_material_status_t::INVALID_METADATA;
            result.iFeature = iFeature;
            return result;
        }
    }
    for ( usize iTexture = 0u;
          iTexture < metadata.nTextures;
          ++iTexture ) {
        if ( !ReadTextureRecordV2( reader, textureRecords[iTexture] ) ) {
            result.status = cooked_material_status_t::INVALID_METADATA;
            result.iTexture = iTexture;
            return result;
        }
    }
    for ( usize iParameter = 0u;
          iParameter < metadata.nParameters;
          ++iParameter ) {
        if ( !ReadParameterRecordV2(
                 reader,
                 parameterRecords[iParameter] ) ) {
            result.status = cooked_material_status_t::INVALID_METADATA;
            result.iParameter = iParameter;
            return result;
        }
    }
    if ( ByteReader_Remaining( &reader ) != 0u ) {
        result.status = cooked_material_status_t::INVALID_METADATA;
        return result;
    }

    cooked_material_view_t material{};
    material.nResourceVersion = CY_COOKED_MATERIAL_RESOURCE_VERSION_V2;
    material.flags = metadata.flags;
    material.domain = metadata.domain;
    material.alphaMode = metadata.alphaMode;
    material.alphaCutoff = alphaCutoff;
    material.nFeatures = metadata.nFeatures;
    material.nTextures = metadata.nTextures;
    material.nParameters = metadata.nParameters;
    material.shaderInterfaceHash = metadata.shaderInterfaceHash;
    material.variantHash = metadata.variantHash;
    material.constantData = constantBytes;
    material.sourceHash = header.sourceHash;

    usize iString = 0u;
    if ( !ConsumeCanonicalString(
             stringBytes,
             metadata.shader,
             iString,
             material.shader ) ||
         !DataValidation_Succeeded(
             DataValidation_CheckResourcePath(
                 material.shader,
                 MaterialText( ".cyshader" ),
                 CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
        result.status = cooked_material_status_t::INVALID_SHADER_PATH;
        return result;
    }
    if ( metadata.surface.cchLength == 0u ) {
        if ( metadata.surface.iOffset != 0u ) {
            result.status = cooked_material_status_t::INVALID_SURFACE_PATH;
            return result;
        }
    } else {
        if ( !ConsumeCanonicalString(
                 stringBytes,
                 metadata.surface,
                 iString,
                 material.surface ) ||
             !DataValidation_Succeeded(
                 DataValidation_CheckResourcePath(
                     material.surface,
                     MaterialText( ".cysurface" ),
                     CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ) {
            result.status = cooked_material_status_t::INVALID_SURFACE_PATH;
            return result;
        }
        material.bHasSurface = CY_TRUE;
    }

    cooked_material_feature_source_v2_t
        variantFeatures[CY_RENDER_MATERIAL_MAX_FEATURES]{};
    for ( usize iFeature = 0u;
          iFeature < material.nFeatures;
          ++iFeature ) {
        const material_feature_record_v2_t &record =
            featureRecords[iFeature];
        cooked_material_feature_view_t &feature =
            material.features[iFeature];
        if ( !ConsumeCanonicalString(
                 stringBytes,
                 record.name,
                 iString,
                 feature.name ) ||
             !DataValidation_Succeeded(
                 DataValidation_CheckAsciiIdentifier(
                     feature.name,
                     CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ||
             !IsFeatureTypeValid( record.type ) ) {
            result.status = cooked_material_status_t::INVALID_FEATURE;
            result.iFeature = iFeature;
            return result;
        }
        feature.type = record.type;
        if ( record.type == cooked_material_feature_value_type_t::BOOL ) {
            if ( record.bValue > 1u || record.enumValue.iOffset != 0u ||
                 record.enumValue.cchLength != 0u ) {
                result.status = cooked_material_status_t::INVALID_FEATURE;
                result.iFeature = iFeature;
                return result;
            }
            feature.bValue = record.bValue != 0u;
        } else {
            if ( record.bValue != 0u ||
                 !ConsumeCanonicalString(
                     stringBytes,
                     record.enumValue,
                     iString,
                     feature.enumValue ) ||
                 !DataValidation_Succeeded(
                     DataValidation_CheckAsciiIdentifier(
                         feature.enumValue,
                         CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ) {
                result.status = cooked_material_status_t::INVALID_FEATURE;
                result.iFeature = iFeature;
                return result;
            }
        }
        if ( iFeature > 0u ) {
            const i32 order = StringView_Compare(
                material.features[iFeature - 1u].name,
                feature.name );
            if ( order == 0 ) {
                result.status = cooked_material_status_t::DUPLICATE_NAME;
                result.iFeature = iFeature;
                return result;
            }
            if ( order > 0 ) {
                result.status = cooked_material_status_t::NON_CANONICAL_ORDER;
                result.iFeature = iFeature;
                return result;
            }
        }
        variantFeatures[iFeature].name = feature.name;
        variantFeatures[iFeature].type = feature.type;
        variantFeatures[iFeature].bValue = feature.bValue;
        variantFeatures[iFeature].enumValue = feature.enumValue;
    }

    for ( usize iTexture = 0u;
          iTexture < material.nTextures;
          ++iTexture ) {
        const material_texture_record_v2_t &record =
            textureRecords[iTexture];
        cooked_material_texture_view_t &texture =
            material.textures[iTexture];
        u64 expectedId = 0u;
        if ( !ConsumeCanonicalString(
                 stringBytes,
                 record.binding,
                 iString,
                 texture.binding ) ||
             !ConsumeCanonicalString(
                 stringBytes,
                 record.texture,
                 iString,
                 texture.texture ) ||
             !DataValidation_Succeeded(
                 DataValidation_CheckAsciiIdentifier(
                     texture.binding,
                     CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ||
             !DataValidation_Succeeded(
                 DataValidation_CheckResourcePath(
                     texture.texture,
                     MaterialText( ".cytex" ),
                     CY_RENDER_ASSET_PATH_MAX_LENGTH ) ) ||
             !CookedShader_MakeLogicalBindingId(
                 texture.binding,
                 &expectedId ) ||
             record.nLogicalBinding != expectedId ||
             record.nUvSet > CY_RENDER_MATERIAL_MAX_UV_SET ) {
            result.status = cooked_material_status_t::INVALID_TEXTURE;
            result.iTexture = iTexture;
            return result;
        }
        if ( record.sampler.cchLength == 0u ) {
            if ( record.sampler.iOffset != 0u ) {
                result.status = cooked_material_status_t::INVALID_TEXTURE;
                result.iTexture = iTexture;
                return result;
            }
        } else {
            if ( !ConsumeCanonicalString(
                     stringBytes,
                     record.sampler,
                     iString,
                     texture.sampler ) ||
                 !DataValidation_Succeeded(
                     DataValidation_CheckAsciiIdentifier(
                         texture.sampler,
                         CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ) {
                result.status = cooked_material_status_t::INVALID_TEXTURE;
                result.iTexture = iTexture;
                return result;
            }
            texture.bHasSampler = CY_TRUE;
        }
        const f32 values[5]{
            record.uvScale[0], record.uvScale[1],
            record.uvOffset[0], record.uvOffset[1], record.uvRotation
        };
        for ( f32 value : values ) {
            if ( !std::isfinite( value ) ) {
                result.status = cooked_material_status_t::NON_FINITE_VALUE;
                result.iTexture = iTexture;
                return result;
            }
            if ( value == 0.0F && std::signbit( value ) ) {
                result.status = cooked_material_status_t::NON_CANONICAL_LAYOUT;
                result.iTexture = iTexture;
                return result;
            }
        }
        texture.nLogicalBinding = record.nLogicalBinding;
        texture.nUvSet = record.nUvSet;
        texture.uvScale[0] = record.uvScale[0];
        texture.uvScale[1] = record.uvScale[1];
        texture.uvOffset[0] = record.uvOffset[0];
        texture.uvOffset[1] = record.uvOffset[1];
        texture.uvRotation = record.uvRotation;
        if ( iTexture > 0u ) {
            const i32 order = StringView_Compare(
                material.textures[iTexture - 1u].binding,
                texture.binding );
            if ( order == 0 ) {
                result.status = cooked_material_status_t::DUPLICATE_NAME;
                result.iTexture = iTexture;
                return result;
            }
            if ( order > 0 ) {
                result.status = cooked_material_status_t::NON_CANONICAL_ORDER;
                result.iTexture = iTexture;
                return result;
            }
        }
    }

    material_constant_range_t
        ranges[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    usize iExpectedParameterOffset = 0u;
    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        const material_parameter_record_v2_t &record =
            parameterRecords[iParameter];
        cooked_material_parameter_view_t &parameter =
            material.parameters[iParameter];
        u64 expectedId = 0u;
        const u32 cbValue = CookedShader_ValueTypeValueSize( record.type );
        const u32 cbStorage = CookedShader_ValueTypeStorageSize( record.type );
        const u32 nAlignment =
            CookedShader_ValueTypeStorageAlignment( record.type );
        if ( !ConsumeCanonicalString(
                 stringBytes,
                 record.name,
                 iString,
                 parameter.name ) ||
             !DataValidation_Succeeded(
                 DataValidation_CheckAsciiIdentifier(
                     parameter.name,
                     CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ||
             !CookedShader_MakeLogicalBindingId(
                 parameter.name,
                 &expectedId ) ||
             record.nLogicalBinding != expectedId ||
             cbValue == 0u || cbStorage == 0u || nAlignment == 0u ||
             record.cbValue != cbValue || record.cbByteSize != cbStorage ||
             ( record.iByteOffset & ( nAlignment - 1u ) ) != 0u ||
             record.iByteOffset > constantBytes.cbSize ||
             record.cbByteSize >
                 constantBytes.cbSize - record.iByteOffset ) {
            result.status = cooked_material_status_t::INVALID_PARAMETER;
            result.iParameter = iParameter;
            return result;
        }
        usize iAlignedOffset = 0u;
        if ( !Cy_AlignUpChecked(
                 iExpectedParameterOffset,
                 nAlignment,
                 iAlignedOffset ) ||
             record.iByteOffset != iAlignedOffset ) {
            result.status = cooked_material_status_t::INVALID_CONSTANT_DATA;
            result.iParameter = iParameter;
            return result;
        }
        iExpectedParameterOffset = iAlignedOffset + record.cbByteSize;
        result.status = DecodeParameterValueV2(
            record,
            constantBytes,
            parameter );
        if ( result.status != cooked_material_status_t::OK ) {
            result.iParameter = iParameter;
            return result;
        }
        if ( iParameter > 0u ) {
            const i32 order = StringView_Compare(
                material.parameters[iParameter - 1u].name,
                parameter.name );
            if ( order == 0 ) {
                result.status = cooked_material_status_t::DUPLICATE_NAME;
                result.iParameter = iParameter;
                return result;
            }
            if ( order > 0 ) {
                result.status = cooked_material_status_t::NON_CANONICAL_ORDER;
                result.iParameter = iParameter;
                return result;
            }
        }
        ranges[iParameter].iBegin = record.iByteOffset;
        ranges[iParameter].iStorageEnd =
            record.iByteOffset + record.cbByteSize;
        ranges[iParameter].iParameter = iParameter;
    }
    if ( iString != stringBytes.cbSize ) {
        result.status = cooked_material_status_t::INVALID_STRING;
        return result;
    }
    if ( iExpectedParameterOffset != constantBytes.cbSize ) {
        result.status = cooked_material_status_t::NON_CANONICAL_LAYOUT;
        return result;
    }

    for ( usize iTexture = 0u;
          iTexture < material.nTextures;
          ++iTexture ) {
        for ( usize iOther = iTexture + 1u;
              iOther < material.nTextures;
              ++iOther ) {
            if ( material.textures[iTexture].nLogicalBinding ==
                 material.textures[iOther].nLogicalBinding ) {
                result.status = cooked_material_status_t::DUPLICATE_BINDING_ID;
                result.iTexture = iOther;
                return result;
            }
        }
        for ( usize iParameter = 0u;
              iParameter < material.nParameters;
              ++iParameter ) {
            if ( material.textures[iTexture].nLogicalBinding ==
                 material.parameters[iParameter].nLogicalBinding ) {
                result.status = cooked_material_status_t::DUPLICATE_BINDING_ID;
                result.iParameter = iParameter;
                return result;
            }
        }
    }
    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        for ( usize iOther = iParameter + 1u;
              iOther < material.nParameters;
              ++iOther ) {
            if ( material.parameters[iParameter].nLogicalBinding ==
                 material.parameters[iOther].nLogicalBinding ) {
                result.status = cooked_material_status_t::DUPLICATE_BINDING_ID;
                result.iParameter = iOther;
                return result;
            }
        }
    }

    SortConstantRangesV2( ranges, material.nParameters );
    usize iExpectedConstant = 0u;
    for ( usize iRange = 0u;
          iRange < material.nParameters;
          ++iRange ) {
        const material_constant_range_t &range = ranges[iRange];
        if ( range.iBegin < iExpectedConstant ) {
            result.status = cooked_material_status_t::INVALID_CONSTANT_DATA;
            result.iParameter = range.iParameter;
            return result;
        }
        if ( !IsZeroRange(
                 constantBytes,
                 iExpectedConstant,
                 range.iBegin ) ) {
            result.status = cooked_material_status_t::NON_CANONICAL_LAYOUT;
            result.iParameter = range.iParameter;
            return result;
        }
        iExpectedConstant = range.iStorageEnd;
    }
    if ( iExpectedConstant != constantBytes.cbSize ) {
        result.status = cooked_material_status_t::NON_CANONICAL_LAYOUT;
        return result;
    }

    content_hash_t variantHash{};
    if ( !HashCanonicalFeaturesV2(
             variantFeatures,
             material.nFeatures,
             variantHash ) ||
         !ContentHash_Equals( variantHash, material.variantHash ) ) {
        result.status = cooked_material_status_t::INVALID_VARIANT_HASH;
        return result;
    }

    usize iExpected = CookedResource_PrefixSize( header.nChunks );
    usize iPrevious = iExpected;
    if ( !Cy_AlignUpChecked(
             iExpected,
             CY_COOKED_MATERIAL_METADATA_ALIGNMENT,
             iExpected ) ||
         metadataChunk.iOffset != iExpected ||
         !IsZeroRange( input, iPrevious, iExpected ) ||
         !CheckedAdd(
             iExpected,
             static_cast<usize>( metadataChunk.cbStored ),
             iExpected ) ) {
        result.status = cooked_material_status_t::NON_CANONICAL_LAYOUT;
        return result;
    }
    if ( bHasConstants ) {
        iPrevious = iExpected;
        if ( !Cy_AlignUpChecked(
                 iExpected,
                 CY_COOKED_MATERIAL_CONSTANT_ALIGNMENT,
                 iExpected ) ||
             chunks[1].iOffset != iExpected ||
             !IsZeroRange( input, iPrevious, iExpected ) ||
             !CheckedAdd(
                 iExpected,
                 static_cast<usize>( chunks[1].cbStored ),
                 iExpected ) ) {
            result.status = cooked_material_status_t::NON_CANONICAL_LAYOUT;
            return result;
        }
    }
    if ( stringChunk.iOffset != iExpected ||
         !CheckedAdd(
             iExpected,
             static_cast<usize>( stringChunk.cbStored ),
             iExpected ) ||
         iExpected != input.cbSize ) {
        result.status = cooked_material_status_t::NON_CANONICAL_LAYOUT;
        return result;
    }

    *pMaterialOut = material;
    result.cbRead = input.cbSize;
    return result;
}

} // namespace

bool_t CookedMaterial_ComputeVariantHash(
    span_t<const cooked_material_feature_source_v2_t> features,
    content_hash_t *pVariantHashOut ) noexcept
{
    if ( pVariantHashOut == nullptr || !Span_IsValid( features ) ||
         features.nCount > CY_RENDER_MATERIAL_MAX_FEATURES ||
         Cy_MemRangesOverlap(
             features.pData,
             features.nCount *
                 sizeof( cooked_material_feature_source_v2_t ),
             pVariantHashOut,
             sizeof( *pVariantHashOut ) ) ) {
        return CY_FALSE;
    }
    cooked_material_feature_source_v2_t
        canonical[CY_RENDER_MATERIAL_MAX_FEATURES]{};
    for ( usize iFeature = 0u; iFeature < features.nCount; ++iFeature ) {
        const cooked_material_feature_source_v2_t &feature =
            features.pData[iFeature];
        if ( Cy_MemRangesOverlap(
                 feature.name.pData,
                 feature.name.cchLength,
                 pVariantHashOut,
                 sizeof( *pVariantHashOut ) ) ||
             ( feature.type == cooked_material_feature_value_type_t::ENUM &&
               Cy_MemRangesOverlap(
                   feature.enumValue.pData,
                   feature.enumValue.cchLength,
                   pVariantHashOut,
                   sizeof( *pVariantHashOut ) ) ) ) {
            return CY_FALSE;
        }
        if ( !DataValidation_Succeeded(
                 DataValidation_CheckAsciiIdentifier(
                     feature.name,
                     CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ||
             !IsFeatureTypeValid( feature.type ) ||
             ( feature.type == cooked_material_feature_value_type_t::ENUM &&
               !DataValidation_Succeeded(
                   DataValidation_CheckAsciiIdentifier(
                       feature.enumValue,
                       CY_RENDER_ASSET_IDENTIFIER_MAX_LENGTH ) ) ) ||
             ( feature.type == cooked_material_feature_value_type_t::BOOL &&
               ( !StringView_IsValid( feature.enumValue ) ||
                 feature.enumValue.cchLength != 0u ) ) ) {
            return CY_FALSE;
        }
        canonical[iFeature] = feature;
    }
    SortNamedV2( canonical, features.nCount );
    for ( usize iFeature = 1u; iFeature < features.nCount; ++iFeature ) {
        if ( StringView_Equals(
                 canonical[iFeature - 1u].name,
                 canonical[iFeature].name ) ) {
            return CY_FALSE;
        }
    }
    content_hash_t hash{};
    if ( !HashCanonicalFeaturesV2(
             canonical,
             features.nCount,
             hash ) ) {
        return CY_FALSE;
    }
    *pVariantHashOut = hash;
    return CY_TRUE;
}

usize CookedMaterial_RequiredSizeV2(
    const cooked_material_source_v2_t &material ) noexcept
{
    canonical_material_v2_t canonical{};
    if ( CanonicalizeMaterialV2(
             material,
             canonical,
             nullptr ) != cooked_material_status_t::OK ) {
        return 0u;
    }
    cooked_chunk_desc_t chunks[3]{};
    u32 nChunks = 0u;
    usize cbFile = 0u;
    return PrepareCanonicalLayoutV2(
               canonical,
               chunks,
               nChunks,
               cbFile )
        ? cbFile
        : 0u;
}

cooked_material_result_t CookedMaterial_WriteV2(
    const cooked_material_source_v2_t &material,
    content_hash_t sourceHash,
    byte_span_t output ) noexcept
{
    cooked_material_result_t result{};
    if ( !Span_IsValid( output ) ) {
        result.status = cooked_material_status_t::INVALID_ARGUMENT;
        return result;
    }
    canonical_material_v2_t canonical{};
    result.status = CanonicalizeMaterialV2(
        material,
        canonical,
        &result );
    if ( result.status != cooked_material_status_t::OK ) {
        return result;
    }

    cooked_chunk_desc_t chunks[3]{};
    u32 nChunks = 0u;
    if ( !PrepareCanonicalLayoutV2(
             canonical,
             chunks,
             nChunks,
             result.cbRequired ) ) {
        result.status = cooked_material_status_t::INVALID_METADATA;
        return result;
    }
    if ( output.nCount < result.cbRequired ) {
        result.status = cooked_material_status_t::OUTPUT_TOO_SMALL;
        return result;
    }
    if ( OutputOverlapsMaterialV2(
             output,
             result.cbRequired,
             material,
             canonical ) ) {
        result.status = cooked_material_status_t::INVALID_ARGUMENT;
        return result;
    }

    Cy_MemZero( output.pData, result.cbRequired );
    const usize iStringChunk = nChunks - 1u;
    const byte_span_t metadataOutput{
        output.pData + chunks[0].iOffset,
        static_cast<usize>( chunks[0].cbStored )
    };
    const byte_span_t stringOutput{
        output.pData + chunks[iStringChunk].iOffset,
        static_cast<usize>( chunks[iStringChunk].cbStored )
    };
    if ( !WriteMetadataV2( canonical, metadataOutput ) ||
         !WriteStringsV2( canonical, stringOutput ) ) {
        result.status = cooked_material_status_t::INVALID_METADATA;
        return result;
    }
    if ( canonical.cbConstantData != 0u ) {
        const byte_span_t constantOutput{
            output.pData + chunks[1].iOffset,
            static_cast<usize>( chunks[1].cbStored )
        };
        if ( !WriteConstantsV2( canonical, constantOutput ) ) {
            result.status = cooked_material_status_t::INVALID_CONSTANT_DATA;
            return result;
        }
        chunks[1].contentHash = ContentHash_Data( {
            constantOutput.pData,
            constantOutput.nCount
        } );
    }
    chunks[0].contentHash = ContentHash_Data( {
        metadataOutput.pData,
        metadataOutput.nCount
    } );
    chunks[iStringChunk].contentHash = ContentHash_Data( {
        stringOutput.pData,
        stringOutput.nCount
    } );

    cooked_resource_header_t header{};
    header.resourceType = CY_RENDER_MATERIAL_RESOURCE_TYPE;
    header.nResourceVersion = CY_COOKED_MATERIAL_RESOURCE_VERSION_V2;
    header.nChunks = nChunks;
    header.cbFile = result.cbRequired;
    if ( ContentHash_IsValid( sourceHash ) ) {
        header.flags |= COOKED_RESOURCE_FLAG_HAS_SOURCE_HASH;
        header.sourceHash = sourceHash;
    }
    cooked_resource_result_t layout = CookedResource_WriteLayout(
        header,
        { chunks, nChunks },
        output );
    if ( !CookedResource_Succeeded( layout ) ) {
        result.status = cooked_material_status_t::RESOURCE_ERROR;
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
        { chunks, nChunks },
        output );
    if ( !CookedResource_Succeeded( layout ) ) {
        result.status = cooked_material_status_t::RESOURCE_ERROR;
        result.resourceStatus = layout.status;
        result.iChunk = layout.iChunk;
        return result;
    }
    result.cbWritten = result.cbRequired;
    return result;
}

cooked_material_result_t CookedMaterial_Read(
    binary_block_t input,
    cooked_material_view_t *pMaterialOut ) noexcept
{
    cooked_material_result_t result{};
    if ( !BinaryBlock_IsValid( input ) || pMaterialOut == nullptr ||
         Cy_MemRangesOverlap(
             input.pData,
             input.cbSize,
             pMaterialOut,
             sizeof( *pMaterialOut ) ) ) {
        result.status = cooked_material_status_t::INVALID_ARGUMENT;
        return result;
    }

    cooked_resource_header_t header{};
    cooked_chunk_desc_t chunks[3]{};
    const cooked_resource_result_t layout = CookedResource_ReadLayout(
        input,
        &header,
        { chunks, 3u } );
    if ( !CookedResource_Succeeded( layout ) ) {
        result.status = cooked_material_status_t::RESOURCE_ERROR;
        result.resourceStatus = layout.status;
        result.iChunk = layout.iChunk;
        return result;
    }
    if ( header.resourceType != CY_RENDER_MATERIAL_RESOURCE_TYPE ) {
        result.status = cooked_material_status_t::INVALID_RESOURCE_TYPE;
        return result;
    }
    if ( header.nResourceVersion ==
         CY_COOKED_MATERIAL_RESOURCE_VERSION_V1 ) {
        return ReadMaterialV1( input, pMaterialOut );
    }
    if ( header.nResourceVersion ==
         CY_COOKED_MATERIAL_RESOURCE_VERSION_V2 ) {
        return ReadMaterialV2( input, pMaterialOut );
    }
    result.status = cooked_material_status_t::VERSION_MISMATCH;
    return result;
}

const cooked_material_texture_view_t *CookedMaterial_FindTexture(
    const cooked_material_view_t &material,
    string_view_t binding ) noexcept
{
    if ( !StringView_IsValid( binding ) ||
         material.nTextures > CY_RENDER_MATERIAL_MAX_TEXTURES ) {
        return nullptr;
    }
    // Canonical sorted bindings support allocation-free logarithmic lookup.
    usize iBegin = 0u;
    usize iEnd = material.nTextures;
    while ( iBegin < iEnd ) {
        const usize iMiddle = iBegin + ( iEnd - iBegin ) / 2u;
        const i32 order = StringView_Compare(
            material.textures[iMiddle].binding,
            binding );
        if ( order < 0 ) {
            iBegin = iMiddle + 1u;
        } else {
            iEnd = iMiddle;
        }
    }
    return iBegin < material.nTextures &&
           StringView_Equals(
               material.textures[iBegin].binding,
               binding )
        ? &material.textures[iBegin]
        : nullptr;
}

const cooked_material_texture_view_t *CookedMaterial_FindTextureById(
    const cooked_material_view_t &material,
    u64 nLogicalBinding ) noexcept
{
    if ( nLogicalBinding == 0u ||
         material.nTextures > CY_RENDER_MATERIAL_MAX_TEXTURES ) {
        return nullptr;
    }
    for ( usize iTexture = 0u;
          iTexture < material.nTextures;
          ++iTexture ) {
        if ( material.textures[iTexture].nLogicalBinding ==
             nLogicalBinding ) {
            return &material.textures[iTexture];
        }
    }
    return nullptr;
}

const cooked_material_parameter_view_t *CookedMaterial_FindParameter(
    const cooked_material_view_t &material,
    string_view_t name ) noexcept
{
    if ( !StringView_IsValid( name ) ||
         material.nParameters > CY_RENDER_MATERIAL_MAX_PARAMETERS ) {
        return nullptr;
    }
    usize iBegin = 0u;
    usize iEnd = material.nParameters;
    while ( iBegin < iEnd ) {
        const usize iMiddle = iBegin + ( iEnd - iBegin ) / 2u;
        const i32 order = StringView_Compare(
            material.parameters[iMiddle].name,
            name );
        if ( order < 0 ) {
            iBegin = iMiddle + 1u;
        } else {
            iEnd = iMiddle;
        }
    }
    return iBegin < material.nParameters &&
           StringView_Equals( material.parameters[iBegin].name, name )
        ? &material.parameters[iBegin]
        : nullptr;
}

const cooked_material_parameter_view_t *CookedMaterial_FindParameterById(
    const cooked_material_view_t &material,
    u64 nLogicalBinding ) noexcept
{
    if ( nLogicalBinding == 0u ||
         material.nParameters > CY_RENDER_MATERIAL_MAX_PARAMETERS ) {
        return nullptr;
    }
    for ( usize iParameter = 0u;
          iParameter < material.nParameters;
          ++iParameter ) {
        if ( material.parameters[iParameter].nLogicalBinding ==
             nLogicalBinding ) {
            return &material.parameters[iParameter];
        }
    }
    return nullptr;
}

bool_t CookedMaterial_Succeeded(
    const cooked_material_result_t &result ) noexcept
{
    return result.status == cooked_material_status_t::OK;
}

const char *CookedMaterial_StatusName(
    cooked_material_status_t status ) noexcept
{
    switch ( status ) {
        case cooked_material_status_t::OK: return "OK";
        case cooked_material_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case cooked_material_status_t::OUTPUT_TOO_SMALL: return "OUTPUT_TOO_SMALL";
        case cooked_material_status_t::RESOURCE_ERROR: return "RESOURCE_ERROR";
        case cooked_material_status_t::INVALID_RESOURCE_TYPE: return "INVALID_RESOURCE_TYPE";
        case cooked_material_status_t::VERSION_MISMATCH: return "VERSION_MISMATCH";
        case cooked_material_status_t::INVALID_CHUNK_COUNT: return "INVALID_CHUNK_COUNT";
        case cooked_material_status_t::INVALID_METADATA_CHUNK: return "INVALID_METADATA_CHUNK";
        case cooked_material_status_t::INVALID_CONSTANT_CHUNK: return "INVALID_CONSTANT_CHUNK";
        case cooked_material_status_t::INVALID_STRING_CHUNK: return "INVALID_STRING_CHUNK";
        case cooked_material_status_t::INVALID_METADATA: return "INVALID_METADATA";
        case cooked_material_status_t::INVALID_FLAGS: return "INVALID_FLAGS";
        case cooked_material_status_t::TEXTURE_LIMIT_EXCEEDED: return "TEXTURE_LIMIT_EXCEEDED";
        case cooked_material_status_t::PARAMETER_LIMIT_EXCEEDED: return "PARAMETER_LIMIT_EXCEEDED";
        case cooked_material_status_t::FEATURE_LIMIT_EXCEEDED: return "FEATURE_LIMIT_EXCEEDED";
        case cooked_material_status_t::INVALID_SHADER_PATH: return "INVALID_SHADER_PATH";
        case cooked_material_status_t::INVALID_SURFACE_PATH: return "INVALID_SURFACE_PATH";
        case cooked_material_status_t::INVALID_DOMAIN: return "INVALID_DOMAIN";
        case cooked_material_status_t::INVALID_ALPHA_MODE: return "INVALID_ALPHA_MODE";
        case cooked_material_status_t::INVALID_FEATURE: return "INVALID_FEATURE";
        case cooked_material_status_t::INVALID_TEXTURE: return "INVALID_TEXTURE";
        case cooked_material_status_t::INVALID_PARAMETER: return "INVALID_PARAMETER";
        case cooked_material_status_t::INVALID_INTERFACE_HASH: return "INVALID_INTERFACE_HASH";
        case cooked_material_status_t::INVALID_VARIANT_HASH: return "INVALID_VARIANT_HASH";
        case cooked_material_status_t::INVALID_CONSTANT_DATA: return "INVALID_CONSTANT_DATA";
        case cooked_material_status_t::INVALID_STRING: return "INVALID_STRING";
        case cooked_material_status_t::DUPLICATE_NAME: return "DUPLICATE_NAME";
        case cooked_material_status_t::DUPLICATE_BINDING_ID: return "DUPLICATE_BINDING_ID";
        case cooked_material_status_t::NON_CANONICAL_ORDER: return "NON_CANONICAL_ORDER";
        case cooked_material_status_t::NON_FINITE_VALUE: return "NON_FINITE_VALUE";
        case cooked_material_status_t::CONTENT_HASH_MISMATCH: return "CONTENT_HASH_MISMATCH";
        case cooked_material_status_t::NON_CANONICAL_LAYOUT: return "NON_CANONICAL_LAYOUT";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
