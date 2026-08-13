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

inline constexpr flags32_t CY_COOKED_MATERIAL_KNOWN_FLAGS =
    COOKED_MATERIAL_FLAG_NONE;

struct material_string_ref_t {
    u32 iOffset{ 0u };
    u32 cchLength{ 0u };
};

struct material_texture_record_t {
    material_string_ref_t binding{};
    material_string_ref_t texture{};
};

struct material_parameter_record_t {
    material_string_ref_t name{};
    render_material_parameter_type_t type{
        render_material_parameter_type_t::SCALAR
    };
    u32 nComponents{ 0u };
    f64 values[CY_RENDER_MATERIAL_VECTOR_MAX_COMPONENTS]{};
};

struct material_metadata_t {
    flags32_t flags{ COOKED_MATERIAL_FLAG_NONE };
    u32 nTextures{ 0u };
    u32 nParameters{ 0u };
    material_string_ref_t shader{};
    u32 cbStringTable{ 0u };
};

struct canonical_material_t {
    string_view_t shader{};
    cooked_material_texture_source_t
        textures[CY_RENDER_MATERIAL_MAX_TEXTURES]{};
    cooked_material_parameter_source_t
        parameters[CY_RENDER_MATERIAL_MAX_PARAMETERS]{};
    usize nTextures{ 0u };
    usize nParameters{ 0u };
    usize cbStringTable{ 0u };
    flags32_t flags{ COOKED_MATERIAL_FLAG_NONE };
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
    if ( ( material.flags & ~CY_COOKED_MATERIAL_KNOWN_FLAGS ) != 0u ) {
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
    header.nResourceVersion = CY_RENDER_MATERIAL_RESOURCE_VERSION;
    header.nChunks = 2u;
    header.cbFile = result.cbRequired;
    if ( ContentHash_IsValid( sourceHash ) ) {
        header.flags |= COOKED_RESOURCE_FLAG_HAS_SOURCE_HASH;
        header.sourceHash = sourceHash;
    }

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
    if ( header.nResourceVersion != CY_RENDER_MATERIAL_RESOURCE_VERSION ) {
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
    if ( ( metadata.flags & ~CY_COOKED_MATERIAL_KNOWN_FLAGS ) != 0u ) {
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

    cooked_material_view_t material{};
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

const cooked_material_texture_view_t *CookedMaterial_FindTexture(
    const cooked_material_view_t &material,
    string_view_t binding ) noexcept
{
    if ( !StringView_IsValid( binding ) ||
         material.nTextures > CY_RENDER_MATERIAL_MAX_TEXTURES ) {
        return nullptr;
    }
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
        case cooked_material_status_t::INVALID_STRING_CHUNK: return "INVALID_STRING_CHUNK";
        case cooked_material_status_t::INVALID_METADATA: return "INVALID_METADATA";
        case cooked_material_status_t::INVALID_FLAGS: return "INVALID_FLAGS";
        case cooked_material_status_t::TEXTURE_LIMIT_EXCEEDED: return "TEXTURE_LIMIT_EXCEEDED";
        case cooked_material_status_t::PARAMETER_LIMIT_EXCEEDED: return "PARAMETER_LIMIT_EXCEEDED";
        case cooked_material_status_t::INVALID_SHADER_PATH: return "INVALID_SHADER_PATH";
        case cooked_material_status_t::INVALID_TEXTURE: return "INVALID_TEXTURE";
        case cooked_material_status_t::INVALID_PARAMETER: return "INVALID_PARAMETER";
        case cooked_material_status_t::INVALID_STRING: return "INVALID_STRING";
        case cooked_material_status_t::DUPLICATE_NAME: return "DUPLICATE_NAME";
        case cooked_material_status_t::NON_CANONICAL_ORDER: return "NON_CANONICAL_ORDER";
        case cooked_material_status_t::NON_FINITE_VALUE: return "NON_FINITE_VALUE";
        case cooked_material_status_t::CONTENT_HASH_MISMATCH: return "CONTENT_HASH_MISMATCH";
        case cooked_material_status_t::NON_CANONICAL_LAYOUT: return "NON_CANONICAL_LAYOUT";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
