//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_CookedTexture.cpp
//  Purpose: Implements the backend-neutral cooked texture resource contract.
//  Details: Writers produce a canonical CYRS layout with one metadata chunk and
//           one independently hashed chunk per mip level. Readers validate the
//           complete chain and publish borrowed views only after all checks pass.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CookedTexture.h"

#include "CypherCommon_ByteReader.h"
#include "CypherCommon_ByteWriter.h"
#include "CypherCommon_MemoryOps.h"

namespace cypher::common
{

namespace
{

inline constexpr flags32_t CY_COOKED_TEXTURE_KNOWN_FLAGS =
    COOKED_TEXTURE_FLAG_GENERATED_MIPS;

struct cooked_texture_metadata_t {
    cooked_texture_desc_t texture{};
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

CYPHER_NODISCARD bool_t CheckedMultiplyU64(
    u64 left,
    u64 right,
    u64 &valueOut ) noexcept
{
    if ( left != 0u && right > CY_U64_MAX / left ) {
        valueOut = 0u;
        return CY_FALSE;
    }
    valueOut = left * right;
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

CYPHER_NODISCARD bool_t IsDimensionValid(
    render_texture_dimension_t dimension ) noexcept
{
    return dimension == render_texture_dimension_t::TEXTURE_2D;
}

CYPHER_NODISCARD bool_t IsPixelFormatValid(
    render_texture_pixel_format_t pixelFormat ) noexcept
{
    return pixelFormat == render_texture_pixel_format_t::RGBA8_UNORM ||
           pixelFormat == render_texture_pixel_format_t::RGBA8_SRGB ||
           pixelFormat == render_texture_pixel_format_t::RGBA32_FLOAT;
}

CYPHER_NODISCARD bool_t IsUsageValid(
    render_texture_usage_t usage ) noexcept
{
    return usage == render_texture_usage_t::COLOR ||
           usage == render_texture_usage_t::NORMAL ||
           usage == render_texture_usage_t::DATA;
}

CYPHER_NODISCARD bool_t IsColorSpaceValid(
    render_texture_color_space_t colorSpace ) noexcept
{
    return colorSpace == render_texture_color_space_t::SRGB ||
           colorSpace == render_texture_color_space_t::LINEAR;
}

CYPHER_NODISCARD cooked_texture_status_t ValidateTexture(
    const cooked_texture_desc_t &texture ) noexcept
{
    if ( !IsDimensionValid( texture.dimension ) ) {
        return cooked_texture_status_t::INVALID_DIMENSION;
    }
    if ( !IsPixelFormatValid( texture.pixelFormat ) ) {
        return cooked_texture_status_t::INVALID_PIXEL_FORMAT;
    }
    if ( !IsUsageValid( texture.usage ) ) {
        return cooked_texture_status_t::INVALID_USAGE;
    }
    if ( !IsColorSpaceValid( texture.colorSpace ) ) {
        return cooked_texture_status_t::INVALID_COLOR_SPACE;
    }
    if ( ( texture.flags & ~CY_COOKED_TEXTURE_KNOWN_FLAGS ) != 0u ) {
        return cooked_texture_status_t::INVALID_FLAGS;
    }
    if ( texture.nWidth == 0u || texture.nHeight == 0u ||
         texture.nWidth > CY_COOKED_TEXTURE_MAX_DIMENSION ||
         texture.nHeight > CY_COOKED_TEXTURE_MAX_DIMENSION ||
         texture.nDepth != 1u || texture.nLayers != 1u ||
         texture.nFaces != 1u ) {
        return cooked_texture_status_t::INVALID_EXTENT;
    }

    const u32 nFullMipLevels = CookedTexture_FullMipCount(
        texture.nWidth,
        texture.nHeight,
        texture.nDepth );
    if ( texture.nMipLevels == 0u ||
         texture.nMipLevels > CY_COOKED_TEXTURE_MAX_MIP_LEVELS ||
         texture.nMipLevels > nFullMipLevels ) {
        return cooked_texture_status_t::MIP_LIMIT_EXCEEDED;
    }
    if ( ( texture.flags & COOKED_TEXTURE_FLAG_GENERATED_MIPS ) != 0u &&
         texture.nMipLevels != nFullMipLevels ) {
        return cooked_texture_status_t::INVALID_MIP_CHAIN;
    }

    const bool_t bSrgbFormat =
        texture.pixelFormat == render_texture_pixel_format_t::RGBA8_SRGB;
    if ( ( texture.colorSpace == render_texture_color_space_t::SRGB ) !=
             bSrgbFormat ||
         ( texture.usage != render_texture_usage_t::COLOR &&
           texture.colorSpace != render_texture_color_space_t::LINEAR ) ) {
        return cooked_texture_status_t::INVALID_COMBINATION;
    }
    return cooked_texture_status_t::OK;
}

CYPHER_NODISCARD bool_t ExpectedMipLayout(
    const cooked_texture_desc_t &texture,
    u32 nLevel,
    u32 &nWidthOut,
    u32 &nHeightOut,
    u32 &nDepthOut,
    u32 &cbRowPitchOut,
    u64 &cbDataOut ) noexcept
{
    const u32 cbPixel = CookedTexture_BytesPerPixel( texture.pixelFormat );
    if ( cbPixel == 0u || nLevel >= texture.nMipLevels ) {
        return CY_FALSE;
    }
    nWidthOut = texture.nWidth >> nLevel;
    nHeightOut = texture.nHeight >> nLevel;
    nDepthOut = texture.nDepth >> nLevel;
    if ( nWidthOut == 0u ) {
        nWidthOut = 1u;
    }
    if ( nHeightOut == 0u ) {
        nHeightOut = 1u;
    }
    if ( nDepthOut == 0u ) {
        nDepthOut = 1u;
    }

    const u64 cbRowPitch = static_cast<u64>( nWidthOut ) * cbPixel;
    if ( cbRowPitch > CY_U32_MAX ) {
        return CY_FALSE;
    }
    cbRowPitchOut = static_cast<u32>( cbRowPitch );
    u64 cbSlice = 0u;
    return CheckedMultiplyU64( cbRowPitch, nHeightOut, cbSlice ) &&
           CheckedMultiplyU64( cbSlice, nDepthOut, cbDataOut ) &&
           cbDataOut != 0u &&
           cbDataOut <= CY_COOKED_TEXTURE_MAX_DATA_SIZE;
}

CYPHER_NODISCARD cooked_texture_status_t ValidateMipDescriptors(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_mip_desc_t> mips,
    usize *pInvalidMip ) noexcept
{
    if ( pInvalidMip != nullptr ) {
        *pInvalidMip = CY_INVALID_SIZE;
    }
    if ( !Span_IsValid( mips ) ||
         mips.nCount != texture.nMipLevels ) {
        return cooked_texture_status_t::INVALID_MIP_CHAIN;
    }

    u64 cbTotal = 0u;
    for ( usize iMip = 0u; iMip < mips.nCount; ++iMip ) {
        const cooked_texture_mip_desc_t &mip = mips.pData[iMip];
        u32 nWidth = 0u;
        u32 nHeight = 0u;
        u32 nDepth = 0u;
        u32 cbRowPitch = 0u;
        u64 cbData = 0u;
        const bool_t bExpected = ExpectedMipLayout(
            texture,
            static_cast<u32>( iMip ),
            nWidth,
            nHeight,
            nDepth,
            cbRowPitch,
            cbData );
        if ( !bExpected || mip.nLevel != iMip ||
             mip.nWidth != nWidth || mip.nHeight != nHeight ||
             mip.nDepth != nDepth || mip.cbRowPitch != cbRowPitch ||
             mip.iDataChunk != iMip + 1u || mip.cbData != cbData ||
             cbData > CY_COOKED_TEXTURE_MAX_DATA_SIZE - cbTotal ) {
            if ( pInvalidMip != nullptr ) {
                *pInvalidMip = iMip;
            }
            return cooked_texture_status_t::INVALID_MIP_CHAIN;
        }
        cbTotal += cbData;
    }
    return cooked_texture_status_t::OK;
}

CYPHER_NODISCARD bool_t WriteMetadataHeader(
    byte_writer_t &writer,
    const cooked_texture_desc_t &texture ) noexcept
{
    return ByteWriter_WriteU32( &writer, CY_COOKED_TEXTURE_METADATA_MAGIC ) &&
           ByteWriter_WriteU32(
               &writer,
               CY_COOKED_TEXTURE_METADATA_VERSION ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( CY_COOKED_TEXTURE_METADATA_HEADER_SIZE ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( texture.dimension ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( texture.pixelFormat ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( texture.usage ) ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( texture.colorSpace ) ) &&
           ByteWriter_WriteU32( &writer, texture.flags ) &&
           ByteWriter_WriteU32( &writer, texture.nWidth ) &&
           ByteWriter_WriteU32( &writer, texture.nHeight ) &&
           ByteWriter_WriteU32( &writer, texture.nDepth ) &&
           ByteWriter_WriteU32( &writer, texture.nLayers ) &&
           ByteWriter_WriteU32( &writer, texture.nFaces ) &&
           ByteWriter_WriteU32( &writer, texture.nMipLevels ) &&
           ByteWriter_WriteU32( &writer, 0u ) &&
           ByteWriter_WriteU32( &writer, 0u );
}

CYPHER_NODISCARD bool_t WriteMipDescriptor(
    byte_writer_t &writer,
    const cooked_texture_mip_desc_t &mip ) noexcept
{
    return ByteWriter_WriteU32( &writer, mip.nLevel ) &&
           ByteWriter_WriteU32( &writer, mip.nWidth ) &&
           ByteWriter_WriteU32( &writer, mip.nHeight ) &&
           ByteWriter_WriteU32( &writer, mip.nDepth ) &&
           ByteWriter_WriteU32( &writer, mip.cbRowPitch ) &&
           ByteWriter_WriteU32( &writer, mip.iDataChunk ) &&
           ByteWriter_WriteU64( &writer, mip.cbData );
}

CYPHER_NODISCARD bool_t ReadMetadataHeader(
    byte_reader_t &reader,
    cooked_texture_metadata_t &metadata ) noexcept
{
    u32 magic = 0u;
    u32 version = 0u;
    u32 cbHeader = 0u;
    u32 dimension = 0u;
    u32 pixelFormat = 0u;
    u32 usage = 0u;
    u32 colorSpace = 0u;
    u32 reserved0 = 0u;
    u32 reserved1 = 0u;
    if ( !ByteReader_ReadU32( &reader, &magic ) ||
         !ByteReader_ReadU32( &reader, &version ) ||
         !ByteReader_ReadU32( &reader, &cbHeader ) ||
         !ByteReader_ReadU32( &reader, &dimension ) ||
         !ByteReader_ReadU32( &reader, &pixelFormat ) ||
         !ByteReader_ReadU32( &reader, &usage ) ||
         !ByteReader_ReadU32( &reader, &colorSpace ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.flags ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nWidth ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nHeight ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nDepth ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nLayers ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nFaces ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nMipLevels ) ||
         !ByteReader_ReadU32( &reader, &reserved0 ) ||
         !ByteReader_ReadU32( &reader, &reserved1 ) ) {
        return CY_FALSE;
    }
    if ( magic != CY_COOKED_TEXTURE_METADATA_MAGIC ||
         version != CY_COOKED_TEXTURE_METADATA_VERSION ||
         cbHeader != CY_COOKED_TEXTURE_METADATA_HEADER_SIZE ||
         reserved0 != 0u || reserved1 != 0u ) {
        return CY_FALSE;
    }
    metadata.texture.dimension =
        static_cast<render_texture_dimension_t>( dimension );
    metadata.texture.pixelFormat =
        static_cast<render_texture_pixel_format_t>( pixelFormat );
    metadata.texture.usage = static_cast<render_texture_usage_t>( usage );
    metadata.texture.colorSpace =
        static_cast<render_texture_color_space_t>( colorSpace );
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ReadMipDescriptor(
    byte_reader_t &reader,
    cooked_texture_mip_desc_t &mip ) noexcept
{
    return ByteReader_ReadU32( &reader, &mip.nLevel ) &&
           ByteReader_ReadU32( &reader, &mip.nWidth ) &&
           ByteReader_ReadU32( &reader, &mip.nHeight ) &&
           ByteReader_ReadU32( &reader, &mip.nDepth ) &&
           ByteReader_ReadU32( &reader, &mip.cbRowPitch ) &&
           ByteReader_ReadU32( &reader, &mip.iDataChunk ) &&
           ByteReader_ReadU64( &reader, &mip.cbData );
}

CYPHER_NODISCARD bool_t PrepareCanonicalLayout(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_mip_source_t> mips,
    cooked_chunk_desc_t *pChunks,
    cooked_texture_mip_desc_t *pMipDescs,
    usize &cbFileOut ) noexcept
{
    const u32 nChunks = static_cast<u32>( mips.nCount + 1u );
    usize iOffset = CookedResource_PrefixSize( nChunks );
    if ( iOffset == 0u ||
         !Cy_AlignUpChecked(
             iOffset,
             CY_COOKED_TEXTURE_METADATA_ALIGNMENT,
             iOffset ) ) {
        return CY_FALSE;
    }

    const usize cbMetadata = CookedTexture_MetadataSize(
        static_cast<u32>( mips.nCount ) );
    if ( cbMetadata == 0u ) {
        return CY_FALSE;
    }
    pChunks[0].chunkType = CY_COOKED_TEXTURE_METADATA_CHUNK;
    pChunks[0].nAlignment = CY_COOKED_TEXTURE_METADATA_ALIGNMENT;
    pChunks[0].iOffset = iOffset;
    pChunks[0].cbStored = cbMetadata;
    pChunks[0].cbDecoded = cbMetadata;
    pChunks[0].flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    if ( !CheckedAdd( iOffset, cbMetadata, iOffset ) ) {
        return CY_FALSE;
    }

    for ( usize iMip = 0u; iMip < mips.nCount; ++iMip ) {
        const cooked_texture_mip_source_t &source = mips.pData[iMip];
        if ( !Cy_AlignUpChecked(
                 iOffset,
                 CY_COOKED_TEXTURE_DATA_ALIGNMENT,
                 iOffset ) ) {
            return CY_FALSE;
        }

        cooked_chunk_desc_t &chunk = pChunks[iMip + 1u];
        chunk.chunkType = CY_COOKED_TEXTURE_DATA_CHUNK;
        chunk.nAlignment = CY_COOKED_TEXTURE_DATA_ALIGNMENT;
        chunk.iOffset = iOffset;
        chunk.cbStored = source.pixels.cbSize;
        chunk.cbDecoded = source.pixels.cbSize;
        chunk.flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
        chunk.contentHash = ContentHash_Data( source.pixels );

        pMipDescs[iMip] = {
            static_cast<u32>( iMip ),
            source.nWidth,
            source.nHeight,
            source.nDepth,
            source.cbRowPitch,
            static_cast<u32>( iMip + 1u ),
            source.pixels.cbSize
        };
        if ( !CheckedAdd( iOffset, source.pixels.cbSize, iOffset ) ) {
            return CY_FALSE;
        }
    }
    cbFileOut = iOffset;
    return ValidateMipDescriptors(
               texture,
               { pMipDescs, mips.nCount },
               nullptr ) == cooked_texture_status_t::OK;
}

} // namespace

u32 CookedTexture_BytesPerPixel(
    render_texture_pixel_format_t pixelFormat ) noexcept
{
    switch ( pixelFormat ) {
        case render_texture_pixel_format_t::RGBA8_UNORM:
        case render_texture_pixel_format_t::RGBA8_SRGB:
            return 4u;
        case render_texture_pixel_format_t::RGBA32_FLOAT:
            return 16u;
    }
    return 0u;
}

u32 CookedTexture_FullMipCount(
    u32 nWidth,
    u32 nHeight,
    u32 nDepth ) noexcept
{
    if ( nWidth == 0u || nHeight == 0u || nDepth == 0u ) {
        return 0u;
    }
    u32 nLargest = nWidth;
    if ( nHeight > nLargest ) {
        nLargest = nHeight;
    }
    if ( nDepth > nLargest ) {
        nLargest = nDepth;
    }
    u32 nMipLevels = 1u;
    while ( nLargest > 1u ) {
        nLargest >>= 1u;
        ++nMipLevels;
    }
    return nMipLevels;
}

usize CookedTexture_MetadataSize( u32 nMipLevels ) noexcept
{
    if ( nMipLevels == 0u ||
         nMipLevels > CY_COOKED_TEXTURE_MAX_MIP_LEVELS ) {
        return 0u;
    }
    return CY_COOKED_TEXTURE_METADATA_HEADER_SIZE +
           static_cast<usize>( nMipLevels ) *
               CY_COOKED_TEXTURE_MIP_RECORD_SIZE;
}

usize CookedTexture_RequiredSize(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_mip_source_t> mips ) noexcept
{
    if ( ValidateTexture( texture ) != cooked_texture_status_t::OK ||
         !Span_IsValid( mips ) || mips.nCount != texture.nMipLevels ) {
        return 0u;
    }
    cooked_chunk_desc_t chunks[CY_COOKED_TEXTURE_MAX_MIP_LEVELS + 1u]{};
    cooked_texture_mip_desc_t mipDescs[CY_COOKED_TEXTURE_MAX_MIP_LEVELS]{};
    usize cbFile = 0u;
    if ( !PrepareCanonicalLayout(
             texture,
             mips,
             chunks,
             mipDescs,
             cbFile ) ) {
        return 0u;
    }
    for ( usize iMip = 0u; iMip < mips.nCount; ++iMip ) {
        if ( !BinaryBlock_IsValid( mips.pData[iMip].pixels ) ||
             mips.pData[iMip].pixels.cbSize != mipDescs[iMip].cbData ) {
            return 0u;
        }
    }
    return cbFile;
}

cooked_texture_result_t CookedTexture_WriteMetadata(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_mip_desc_t> mips,
    byte_span_t output ) noexcept
{
    cooked_texture_result_t result{};
    if ( !Span_IsValid( mips ) || !Span_IsValid( output ) ) {
        result.status = cooked_texture_status_t::INVALID_ARGUMENT;
        return result;
    }
    result.cbRequired = CookedTexture_MetadataSize(
        static_cast<u32>( mips.nCount ) );
    if ( result.cbRequired == 0u ) {
        result.status = cooked_texture_status_t::MIP_LIMIT_EXCEEDED;
        return result;
    }
    result.status = ValidateTexture( texture );
    if ( result.status != cooked_texture_status_t::OK ) {
        return result;
    }
    result.status = ValidateMipDescriptors(
        texture,
        mips,
        &result.iMip );
    if ( result.status != cooked_texture_status_t::OK ) {
        return result;
    }
    if ( output.nCount < result.cbRequired ) {
        result.status = cooked_texture_status_t::OUTPUT_TOO_SMALL;
        return result;
    }
    if ( Cy_MemRangesOverlap(
             output.pData,
             result.cbRequired,
             &texture,
             sizeof( texture ) ) ||
         Cy_MemRangesOverlap(
             output.pData,
             result.cbRequired,
             mips.pData,
             mips.nCount * sizeof( cooked_texture_mip_desc_t ) ) ) {
        result.status = cooked_texture_status_t::INVALID_ARGUMENT;
        return result;
    }

    byte_writer_t writer{};
    if ( !ByteWriter_Init(
             &writer,
             output,
             data_byte_order_t::LITTLE ) ||
         !WriteMetadataHeader( writer, texture ) ) {
        result.status = cooked_texture_status_t::OUTPUT_TOO_SMALL;
        return result;
    }
    for ( usize iMip = 0u; iMip < mips.nCount; ++iMip ) {
        if ( !WriteMipDescriptor( writer, mips.pData[iMip] ) ) {
            result.status = cooked_texture_status_t::OUTPUT_TOO_SMALL;
            result.iMip = iMip;
            return result;
        }
    }
    result.cbWritten = ByteWriter_BytesWritten( &writer );
    return result;
}

cooked_texture_result_t CookedTexture_Write(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_mip_source_t> mips,
    content_hash_t sourceHash,
    byte_span_t output ) noexcept
{
    cooked_texture_result_t result{};
    if ( !Span_IsValid( mips ) || !Span_IsValid( output ) ) {
        result.status = cooked_texture_status_t::INVALID_ARGUMENT;
        return result;
    }
    result.status = ValidateTexture( texture );
    if ( result.status != cooked_texture_status_t::OK ) {
        return result;
    }
    if ( mips.nCount != texture.nMipLevels ) {
        result.status = cooked_texture_status_t::INVALID_MIP_CHAIN;
        return result;
    }

    cooked_chunk_desc_t chunks[CY_COOKED_TEXTURE_MAX_MIP_LEVELS + 1u]{};
    cooked_texture_mip_desc_t mipDescs[CY_COOKED_TEXTURE_MAX_MIP_LEVELS]{};
    if ( !PrepareCanonicalLayout(
             texture,
             mips,
             chunks,
             mipDescs,
             result.cbRequired ) ) {
        result.status = cooked_texture_status_t::INVALID_MIP_CHAIN;
        return result;
    }
    for ( usize iMip = 0u; iMip < mips.nCount; ++iMip ) {
        if ( !BinaryBlock_IsValid( mips.pData[iMip].pixels ) ||
             mips.pData[iMip].pixels.cbSize != mipDescs[iMip].cbData ) {
            result.status = cooked_texture_status_t::INVALID_DATA;
            result.iMip = iMip;
            return result;
        }
    }
    if ( output.nCount < result.cbRequired ) {
        result.status = cooked_texture_status_t::OUTPUT_TOO_SMALL;
        return result;
    }
    if ( Cy_MemRangesOverlap(
             output.pData,
             result.cbRequired,
             &texture,
             sizeof( texture ) ) ||
         Cy_MemRangesOverlap(
             output.pData,
             result.cbRequired,
             mips.pData,
             mips.nCount * sizeof( cooked_texture_mip_source_t ) ) ) {
        result.status = cooked_texture_status_t::INVALID_ARGUMENT;
        return result;
    }
    for ( usize iMip = 0u; iMip < mips.nCount; ++iMip ) {
        if ( Cy_MemRangesOverlap(
                 output.pData,
                 result.cbRequired,
                 mips.pData[iMip].pixels.pData,
                 mips.pData[iMip].pixels.cbSize ) ) {
            result.status = cooked_texture_status_t::INVALID_ARGUMENT;
            result.iMip = iMip;
            return result;
        }
    }

    const u32 nChunks = static_cast<u32>( mips.nCount + 1u );
    const usize cbPrefix = CookedResource_PrefixSize( nChunks );
    if ( chunks[0].iOffset > cbPrefix ) {
        Cy_MemZero(
            output.pData + cbPrefix,
            static_cast<usize>( chunks[0].iOffset - cbPrefix ) );
    }

    cooked_resource_header_t header{};
    header.resourceType = CY_RENDER_TEXTURE_RESOURCE_TYPE;
    header.nResourceVersion = CY_RENDER_TEXTURE_RESOURCE_VERSION;
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
    const cooked_texture_result_t metadata = CookedTexture_WriteMetadata(
        texture,
        { mipDescs, mips.nCount },
        metadataOutput );
    if ( !CookedTexture_Succeeded( metadata ) ) {
        return metadata;
    }
    chunks[0].contentHash = ContentHash_Data( {
        metadataOutput.pData,
        metadataOutput.nCount
    } );

    usize iPayloadEnd = static_cast<usize>(
        chunks[0].iOffset + chunks[0].cbStored );
    for ( usize iMip = 0u; iMip < mips.nCount; ++iMip ) {
        const cooked_chunk_desc_t &chunk = chunks[iMip + 1u];
        if ( chunk.iOffset > iPayloadEnd ) {
            Cy_MemZero(
                output.pData + iPayloadEnd,
                static_cast<usize>( chunk.iOffset - iPayloadEnd ) );
        }
        Cy_MemCopy(
            output.pData + chunk.iOffset,
            mips.pData[iMip].pixels.pData,
            mips.pData[iMip].pixels.cbSize );
        iPayloadEnd = static_cast<usize>( chunk.iOffset + chunk.cbStored );
    }

    const cooked_resource_result_t layout = CookedResource_WriteLayout(
        header,
        { chunks, nChunks },
        output );
    if ( !CookedResource_Succeeded( layout ) ) {
        result.status = cooked_texture_status_t::RESOURCE_ERROR;
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
        { chunks, nChunks },
        output );
    if ( !CookedResource_Succeeded( sealed ) ) {
        result.status = cooked_texture_status_t::RESOURCE_ERROR;
        result.resourceStatus = sealed.status;
        result.iChunk = sealed.iChunk;
        return result;
    }

    result.cbWritten = result.cbRequired;
    return result;
}

cooked_texture_result_t CookedTexture_Read(
    binary_block_t input,
    cooked_texture_view_t *pTextureOut ) noexcept
{
    cooked_texture_result_t result{};
    if ( !BinaryBlock_IsValid( input ) || pTextureOut == nullptr ||
         Cy_MemRangesOverlap(
             input.pData,
             input.cbSize,
             pTextureOut,
             sizeof( *pTextureOut ) ) ) {
        result.status = cooked_texture_status_t::INVALID_ARGUMENT;
        return result;
    }

    cooked_resource_header_t header{};
    cooked_chunk_desc_t chunks[CY_COOKED_TEXTURE_MAX_MIP_LEVELS + 1u]{};
    const cooked_resource_result_t layout = CookedResource_ReadLayout(
        input,
        &header,
        { chunks, CY_COOKED_TEXTURE_MAX_MIP_LEVELS + 1u } );
    if ( !CookedResource_Succeeded( layout ) ) {
        result.status = cooked_texture_status_t::RESOURCE_ERROR;
        result.resourceStatus = layout.status;
        result.iChunk = layout.iChunk;
        return result;
    }
    if ( header.resourceType != CY_RENDER_TEXTURE_RESOURCE_TYPE ) {
        result.status = cooked_texture_status_t::INVALID_RESOURCE_TYPE;
        return result;
    }
    if ( header.nResourceVersion != CY_RENDER_TEXTURE_RESOURCE_VERSION ) {
        result.status = cooked_texture_status_t::VERSION_MISMATCH;
        return result;
    }
    if ( ( header.flags & COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH ) == 0u ) {
        result.status = cooked_texture_status_t::INVALID_FLAGS;
        return result;
    }
    if ( header.nChunks < 2u ||
         header.nChunks > CY_COOKED_TEXTURE_MAX_MIP_LEVELS + 1u ) {
        result.status = cooked_texture_status_t::INVALID_CHUNK_COUNT;
        return result;
    }

    const cooked_chunk_desc_t &metadataChunk = chunks[0];
    if ( metadataChunk.chunkType != CY_COOKED_TEXTURE_METADATA_CHUNK ||
         metadataChunk.codec != cooked_chunk_codec_t::NONE ||
         metadataChunk.flags != COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ||
         metadataChunk.nAlignment != CY_COOKED_TEXTURE_METADATA_ALIGNMENT ) {
        result.status = cooked_texture_status_t::INVALID_METADATA_CHUNK;
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
        result.status = cooked_texture_status_t::CONTENT_HASH_MISMATCH;
        result.iChunk = 0u;
        return result;
    }

    byte_reader_t reader{};
    cooked_texture_metadata_t metadata{};
    if ( !ByteReader_Init(
             &reader,
             metadataBytes,
             data_byte_order_t::LITTLE ) ||
         !ReadMetadataHeader( reader, metadata ) ) {
        result.status = cooked_texture_status_t::INVALID_METADATA;
        return result;
    }
    result.status = ValidateTexture( metadata.texture );
    if ( result.status != cooked_texture_status_t::OK ) {
        return result;
    }
    if ( metadata.texture.nMipLevels + 1u != header.nChunks ||
         metadataBytes.cbSize != CookedTexture_MetadataSize(
             metadata.texture.nMipLevels ) ) {
        result.status = cooked_texture_status_t::INVALID_CHUNK_COUNT;
        return result;
    }

    cooked_texture_mip_desc_t
        mipDescs[CY_COOKED_TEXTURE_MAX_MIP_LEVELS]{};
    for ( usize iMip = 0u;
          iMip < metadata.texture.nMipLevels;
          ++iMip ) {
        if ( !ReadMipDescriptor( reader, mipDescs[iMip] ) ) {
            result.status = cooked_texture_status_t::INVALID_METADATA;
            result.iMip = iMip;
            return result;
        }
    }
    result.status = ValidateMipDescriptors(
        metadata.texture,
        { mipDescs, metadata.texture.nMipLevels },
        &result.iMip );
    if ( result.status != cooked_texture_status_t::OK ) {
        return result;
    }

    cooked_texture_view_t texture{};
    texture.desc = metadata.texture;
    texture.sourceHash = header.sourceHash;
    texture.nMipLevels = metadata.texture.nMipLevels;
    usize iPayloadEnd = CookedResource_PrefixSize( header.nChunks );
    usize iExpectedOffset = iPayloadEnd;
    if ( !Cy_AlignUpChecked(
             iExpectedOffset,
             CY_COOKED_TEXTURE_METADATA_ALIGNMENT,
             iExpectedOffset ) ||
         metadataChunk.iOffset != iExpectedOffset ||
         !IsZeroRange( input, iPayloadEnd, iExpectedOffset ) ||
         !CheckedAdd(
             iExpectedOffset,
             static_cast<usize>( metadataChunk.cbStored ),
             iPayloadEnd ) ) {
        result.status = cooked_texture_status_t::NON_CANONICAL_LAYOUT;
        result.iChunk = 0u;
        return result;
    }

    for ( usize iMip = 0u; iMip < texture.nMipLevels; ++iMip ) {
        const cooked_texture_mip_desc_t &mip = mipDescs[iMip];
        const cooked_chunk_desc_t &chunk = chunks[mip.iDataChunk];
        iExpectedOffset = iPayloadEnd;
        if ( !Cy_AlignUpChecked(
                 iExpectedOffset,
                 CY_COOKED_TEXTURE_DATA_ALIGNMENT,
                 iExpectedOffset ) ||
             chunk.iOffset != iExpectedOffset ||
             !IsZeroRange( input, iPayloadEnd, iExpectedOffset ) ||
             !CheckedAdd(
                 iExpectedOffset,
                 static_cast<usize>( chunk.cbStored ),
                 iPayloadEnd ) ) {
            result.status = cooked_texture_status_t::NON_CANONICAL_LAYOUT;
            result.iMip = iMip;
            result.iChunk = mip.iDataChunk;
            return result;
        }
        if ( chunk.chunkType != CY_COOKED_TEXTURE_DATA_CHUNK ||
             chunk.codec != cooked_chunk_codec_t::NONE ||
             chunk.flags != COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ||
             chunk.nAlignment != CY_COOKED_TEXTURE_DATA_ALIGNMENT ||
             chunk.cbStored != mip.cbData ||
             chunk.cbDecoded != mip.cbData ) {
            result.status = cooked_texture_status_t::INVALID_DATA_CHUNK;
            result.iMip = iMip;
            result.iChunk = mip.iDataChunk;
            return result;
        }
        const binary_block_t pixels{
            input.pData + chunk.iOffset,
            static_cast<usize>( chunk.cbStored )
        };
        if ( !ContentHash_Equals(
                 ContentHash_Data( pixels ),
                 chunk.contentHash ) ) {
            result.status = cooked_texture_status_t::CONTENT_HASH_MISMATCH;
            result.iMip = iMip;
            result.iChunk = mip.iDataChunk;
            return result;
        }
        texture.mips[iMip] = {
            mip.nLevel,
            mip.nWidth,
            mip.nHeight,
            mip.nDepth,
            mip.cbRowPitch,
            pixels,
            chunk.contentHash
        };
    }
    if ( iPayloadEnd != input.cbSize ) {
        result.status = cooked_texture_status_t::NON_CANONICAL_LAYOUT;
        return result;
    }

    *pTextureOut = texture;
    result.cbRead = input.cbSize;
    return result;
}

const cooked_texture_mip_view_t *CookedTexture_FindMip(
    const cooked_texture_view_t &texture,
    u32 nLevel ) noexcept
{
    if ( texture.nMipLevels > CY_COOKED_TEXTURE_MAX_MIP_LEVELS ) {
        return nullptr;
    }
    for ( usize iMip = 0u; iMip < texture.nMipLevels; ++iMip ) {
        if ( texture.mips[iMip].nLevel == nLevel ) {
            return &texture.mips[iMip];
        }
    }
    return nullptr;
}

bool_t CookedTexture_Succeeded(
    const cooked_texture_result_t &result ) noexcept
{
    return result.status == cooked_texture_status_t::OK;
}

const char *CookedTexture_StatusName(
    cooked_texture_status_t status ) noexcept
{
    switch ( status ) {
        case cooked_texture_status_t::OK: return "OK";
        case cooked_texture_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case cooked_texture_status_t::OUTPUT_TOO_SMALL: return "OUTPUT_TOO_SMALL";
        case cooked_texture_status_t::RESOURCE_ERROR: return "RESOURCE_ERROR";
        case cooked_texture_status_t::INVALID_RESOURCE_TYPE: return "INVALID_RESOURCE_TYPE";
        case cooked_texture_status_t::VERSION_MISMATCH: return "VERSION_MISMATCH";
        case cooked_texture_status_t::INVALID_CHUNK_COUNT: return "INVALID_CHUNK_COUNT";
        case cooked_texture_status_t::INVALID_METADATA_CHUNK: return "INVALID_METADATA_CHUNK";
        case cooked_texture_status_t::INVALID_METADATA: return "INVALID_METADATA";
        case cooked_texture_status_t::INVALID_DIMENSION: return "INVALID_DIMENSION";
        case cooked_texture_status_t::INVALID_PIXEL_FORMAT: return "INVALID_PIXEL_FORMAT";
        case cooked_texture_status_t::INVALID_USAGE: return "INVALID_USAGE";
        case cooked_texture_status_t::INVALID_COLOR_SPACE: return "INVALID_COLOR_SPACE";
        case cooked_texture_status_t::INVALID_COMBINATION: return "INVALID_COMBINATION";
        case cooked_texture_status_t::INVALID_FLAGS: return "INVALID_FLAGS";
        case cooked_texture_status_t::INVALID_EXTENT: return "INVALID_EXTENT";
        case cooked_texture_status_t::MIP_LIMIT_EXCEEDED: return "MIP_LIMIT_EXCEEDED";
        case cooked_texture_status_t::INVALID_MIP_CHAIN: return "INVALID_MIP_CHAIN";
        case cooked_texture_status_t::INVALID_DATA_CHUNK: return "INVALID_DATA_CHUNK";
        case cooked_texture_status_t::INVALID_DATA: return "INVALID_DATA";
        case cooked_texture_status_t::CONTENT_HASH_MISMATCH: return "CONTENT_HASH_MISMATCH";
        case cooked_texture_status_t::NON_CANONICAL_LAYOUT: return "NON_CANONICAL_LAYOUT";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
