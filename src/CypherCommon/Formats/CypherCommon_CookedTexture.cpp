//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_CookedTexture.cpp
//  Purpose: Implements the backend-neutral cooked texture resource contract.
//  Details: CYTX version 2 stores explicit texture semantics and one canonical,
//           independently hashed chunk per subresource. The reader also accepts
//           canonical CYTX version 1 files and upgrades their metadata in memory.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//  - Extended to the CYTX version 2 subresource contract on 2026-09-17
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CookedTexture.h"

#include "CypherCommon_ByteReader.h"
#include "CypherCommon_ByteWriter.h"
#include "CypherCommon_MemoryOps.h"

#include <cmath>

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

CYPHER_NODISCARD u32 DivideRoundUp( u32 value, u32 divisor ) noexcept
{
    return value / divisor + ( value % divisor != 0u ? 1u : 0u );
}

CYPHER_NODISCARD bool_t IsZeroRange(
    binary_block_t input,
    usize iBegin,
    usize iEnd ) noexcept
{
    return iBegin <= iEnd && iEnd <= input.cbSize &&
           ( iBegin == iEnd ||
             Cy_MemIsZero( input.pData + iBegin, iEnd - iBegin ) );
}

CYPHER_NODISCARD render_format_t LegacyStorageFormat(
    render_texture_pixel_format_t pixelFormat ) noexcept
{
    switch ( pixelFormat ) {
        case render_texture_pixel_format_t::RGBA8_UNORM:
            return render_format_t::RGBA8_UNORM;
        case render_texture_pixel_format_t::RGBA8_SRGB:
            return render_format_t::RGBA8_SRGB;
        case render_texture_pixel_format_t::RGBA32_FLOAT:
            return render_format_t::RGBA32_FLOAT;
        case render_texture_pixel_format_t::UNKNOWN:
            break;
    }
    return render_format_t::UNKNOWN;
}

CYPHER_NODISCARD render_texture_pixel_format_t LegacyPixelFormat(
    render_format_t storageFormat ) noexcept
{
    switch ( storageFormat ) {
        case render_format_t::RGBA8_UNORM:
            return render_texture_pixel_format_t::RGBA8_UNORM;
        case render_format_t::RGBA8_SRGB:
            return render_texture_pixel_format_t::RGBA8_SRGB;
        case render_format_t::RGBA32_FLOAT:
            return render_texture_pixel_format_t::RGBA32_FLOAT;
        default:
            return render_texture_pixel_format_t::UNKNOWN;
    }
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

CYPHER_NODISCARD bool_t IsAlphaModeValid(
    cooked_texture_alpha_mode_t alphaMode,
    bool_t bAllowAuto ) noexcept
{
    return ( bAllowAuto && alphaMode == cooked_texture_alpha_mode_t::AUTO ) ||
           alphaMode == cooked_texture_alpha_mode_t::NONE ||
           alphaMode == cooked_texture_alpha_mode_t::STRAIGHT ||
           alphaMode == cooked_texture_alpha_mode_t::PREMULTIPLIED ||
           alphaMode == cooked_texture_alpha_mode_t::MASK ||
           alphaMode == cooked_texture_alpha_mode_t::DATA;
}

CYPHER_NODISCARD bool_t IsTargetValid(
    cooked_texture_target_t target ) noexcept
{
    return target == cooked_texture_target_t::PORTABLE ||
           target == cooked_texture_target_t::DESKTOP ||
           target == cooked_texture_target_t::APPLE ||
           target == cooked_texture_target_t::MOBILE ||
           target == cooked_texture_target_t::WEB;
}

CYPHER_NODISCARD bool_t IsResidencyValid(
    cooked_texture_residency_t residency ) noexcept
{
    return residency == cooked_texture_residency_t::FULLY_RESIDENT ||
           residency == cooked_texture_residency_t::MIP_STREAMED;
}

CYPHER_NODISCARD bool_t DimensionShapeValid(
    const cooked_texture_desc_t &texture ) noexcept
{
    switch ( texture.dimension ) {
        case render_texture_dimension_t::TEXTURE_1D:
            return texture.nHeight == 1u && texture.nDepth == 1u &&
                   texture.nFaces == 1u;
        case render_texture_dimension_t::TEXTURE_2D:
            return texture.nDepth == 1u && texture.nFaces == 1u;
        case render_texture_dimension_t::TEXTURE_3D:
            return texture.nLayers == 1u && texture.nFaces == 1u;
        case render_texture_dimension_t::TEXTURE_CUBE:
            return texture.nWidth == texture.nHeight && texture.nDepth == 1u &&
                   texture.nFaces == CY_COOKED_TEXTURE_CUBE_FACE_COUNT;
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t IsDimensionValid(
    render_texture_dimension_t dimension ) noexcept
{
    return dimension == render_texture_dimension_t::TEXTURE_1D ||
           dimension == render_texture_dimension_t::TEXTURE_2D ||
           dimension == render_texture_dimension_t::TEXTURE_3D ||
           dimension == render_texture_dimension_t::TEXTURE_CUBE;
}

CYPHER_NODISCARD cooked_texture_alpha_mode_t ResolveAlphaMode(
    const cooked_texture_desc_t &texture,
    const cooked_texture_format_layout_t &layout ) noexcept
{
    if ( texture.alphaMode != cooked_texture_alpha_mode_t::AUTO ) {
        return texture.alphaMode;
    }
    if ( !layout.bHasAlpha ) {
        return cooked_texture_alpha_mode_t::NONE;
    }
    if ( texture.usage == render_texture_usage_t::COLOR ) {
        return cooked_texture_alpha_mode_t::STRAIGHT;
    }
    if ( texture.usage == render_texture_usage_t::DATA ) {
        return cooked_texture_alpha_mode_t::DATA;
    }
    return cooked_texture_alpha_mode_t::NONE;
}

CYPHER_NODISCARD cooked_texture_status_t NormalizeTexture(
    const cooked_texture_desc_t &source,
    bool_t bAllowAuto,
    cooked_texture_desc_t &textureOut ) noexcept
{
    textureOut = source;
    if ( textureOut.storageFormat == render_format_t::UNKNOWN ) {
        textureOut.storageFormat = LegacyStorageFormat( textureOut.pixelFormat );
    }
    cooked_texture_format_layout_t formatLayout{};
    if ( !CookedTexture_FormatLayout(
             textureOut.storageFormat,
             &formatLayout ) ) {
        return cooked_texture_status_t::INVALID_PIXEL_FORMAT;
    }
    textureOut.pixelFormat = LegacyPixelFormat( textureOut.storageFormat );

    if ( !IsUsageValid( textureOut.usage ) ) {
        return cooked_texture_status_t::INVALID_USAGE;
    }
    if ( !IsColorSpaceValid( textureOut.colorSpace ) ) {
        return cooked_texture_status_t::INVALID_COLOR_SPACE;
    }
    if ( !IsAlphaModeValid( textureOut.alphaMode, bAllowAuto ) ) {
        return cooked_texture_status_t::INVALID_ALPHA_MODE;
    }
    textureOut.alphaMode = ResolveAlphaMode( textureOut, formatLayout );
    if ( !std::isfinite( textureOut.alphaCutoff ) ||
         textureOut.alphaCutoff < 0.0f || textureOut.alphaCutoff > 1.0f ) {
        return cooked_texture_status_t::INVALID_ALPHA_MODE;
    }
    if ( !bAllowAuto && textureOut.alphaCutoff == 0.0f &&
         std::signbit( textureOut.alphaCutoff ) ) {
        return cooked_texture_status_t::NON_CANONICAL_LAYOUT;
    }
    textureOut.alphaCutoff = textureOut.alphaCutoff == 0.0f
        ? 0.0f
        : textureOut.alphaCutoff;
    if ( textureOut.alphaMode != cooked_texture_alpha_mode_t::MASK &&
         textureOut.alphaCutoff != 0.5f ) {
        return cooked_texture_status_t::INVALID_COMBINATION;
    }
    if ( !IsTargetValid( textureOut.target ) ) {
        return cooked_texture_status_t::INVALID_TARGET;
    }
    if ( !IsResidencyValid( textureOut.residency ) ) {
        return cooked_texture_status_t::INVALID_RESIDENCY;
    }
    if ( ( textureOut.flags & ~CY_COOKED_TEXTURE_KNOWN_FLAGS ) != 0u ) {
        return cooked_texture_status_t::INVALID_FLAGS;
    }
    if ( !IsDimensionValid( textureOut.dimension ) ) {
        return cooked_texture_status_t::INVALID_DIMENSION;
    }
    if ( textureOut.nWidth == 0u || textureOut.nHeight == 0u ||
         textureOut.nDepth == 0u ||
         textureOut.nWidth > CY_COOKED_TEXTURE_MAX_DIMENSION ||
         textureOut.nHeight > CY_COOKED_TEXTURE_MAX_DIMENSION ||
         textureOut.nDepth > CY_COOKED_TEXTURE_MAX_DIMENSION ||
         textureOut.nLayers == 0u ||
         textureOut.nLayers > CY_COOKED_TEXTURE_MAX_LAYERS ||
         textureOut.nFaces == 0u ||
         textureOut.nFaces > CY_COOKED_TEXTURE_CUBE_FACE_COUNT ||
         textureOut.nFrames == 0u ||
         textureOut.nFrames > CY_COOKED_TEXTURE_MAX_FRAMES ||
         !DimensionShapeValid( textureOut ) ) {
        return cooked_texture_status_t::INVALID_EXTENT;
    }

    const u32 nFullMipLevels = CookedTexture_FullMipCount(
        textureOut.nWidth,
        textureOut.nHeight,
        textureOut.nDepth );
    if ( textureOut.nMipLevels == 0u ||
         textureOut.nMipLevels > CY_COOKED_TEXTURE_MAX_MIP_LEVELS ||
         textureOut.nMipLevels > nFullMipLevels ) {
        return cooked_texture_status_t::MIP_LIMIT_EXCEEDED;
    }
    if ( ( textureOut.flags & COOKED_TEXTURE_FLAG_GENERATED_MIPS ) != 0u &&
         textureOut.nMipLevels != nFullMipLevels ) {
        return cooked_texture_status_t::INVALID_MIP_CHAIN;
    }

    const u32 nSubresources = CookedTexture_SubresourceCount( textureOut );
    if ( nSubresources == 0u ||
         nSubresources > CY_COOKED_TEXTURE_MAX_SUBRESOURCES ) {
        return cooked_texture_status_t::SUBRESOURCE_LIMIT_EXCEEDED;
    }
    if ( textureOut.nSubresources != 0u &&
         textureOut.nSubresources != nSubresources ) {
        return cooked_texture_status_t::INVALID_SUBRESOURCE_LAYOUT;
    }
    textureOut.nSubresources = nSubresources;

    if ( textureOut.residency == cooked_texture_residency_t::FULLY_RESIDENT ) {
        if ( textureOut.nResidentMipLevels != 0u &&
             textureOut.nResidentMipLevels != textureOut.nMipLevels ) {
            return cooked_texture_status_t::INVALID_RESIDENCY;
        }
        textureOut.nResidentMipLevels = textureOut.nMipLevels;
    } else if ( textureOut.nResidentMipLevels == 0u ||
                textureOut.nResidentMipLevels > textureOut.nMipLevels ) {
        return cooked_texture_status_t::INVALID_RESIDENCY;
    }
    if ( textureOut.nStreamingPriority > 255u ) {
        return cooked_texture_status_t::INVALID_RESIDENCY;
    }

    const u32 nSlices = textureOut.nFrames * textureOut.nLayers *
                        textureOut.nFaces;
    const u32 iResident =
        ( textureOut.nMipLevels - textureOut.nResidentMipLevels ) * nSlices;
    const u32 nResident = textureOut.nResidentMipLevels * nSlices;
    if ( !bAllowAuto &&
         ( source.nSubresources != nSubresources ||
           source.iResidentFirstSubresource != iResident ||
           source.nResidentSubresources != nResident ) ) {
        return cooked_texture_status_t::INVALID_RESIDENCY;
    }
    if ( bAllowAuto &&
         ( ( source.iResidentFirstSubresource != 0u &&
             source.iResidentFirstSubresource != iResident ) ||
           ( source.nResidentSubresources != 0u &&
             source.nResidentSubresources != nResident ) ) ) {
        return cooked_texture_status_t::INVALID_RESIDENCY;
    }
    textureOut.iResidentFirstSubresource = iResident;
    textureOut.nResidentSubresources = nResident;

    if ( ( textureOut.colorSpace == render_texture_color_space_t::SRGB ) !=
             formatLayout.bSrgb ||
         ( textureOut.usage != render_texture_usage_t::COLOR &&
           textureOut.colorSpace != render_texture_color_space_t::LINEAR ) ) {
        return cooked_texture_status_t::INVALID_COMBINATION;
    }
    const bool_t bUsesAlpha =
        textureOut.alphaMode == cooked_texture_alpha_mode_t::STRAIGHT ||
        textureOut.alphaMode == cooked_texture_alpha_mode_t::PREMULTIPLIED ||
        textureOut.alphaMode == cooked_texture_alpha_mode_t::MASK ||
        textureOut.alphaMode == cooked_texture_alpha_mode_t::DATA;
    if ( ( bUsesAlpha && !formatLayout.bHasAlpha ) ||
         ( ( textureOut.alphaMode == cooked_texture_alpha_mode_t::STRAIGHT ||
             textureOut.alphaMode == cooked_texture_alpha_mode_t::PREMULTIPLIED ||
             textureOut.alphaMode == cooked_texture_alpha_mode_t::MASK ) &&
           textureOut.usage != render_texture_usage_t::COLOR ) ) {
        return cooked_texture_status_t::INVALID_COMBINATION;
    }
    return cooked_texture_status_t::OK;
}

CYPHER_NODISCARD bool_t ExpectedCoordinates(
    const cooked_texture_desc_t &texture,
    u32 iSubresource,
    u32 &nMipLevelOut,
    u32 &nFrameOut,
    u32 &nLayerOut,
    u32 &nFaceOut ) noexcept
{
    if ( iSubresource >= texture.nSubresources ) {
        return CY_FALSE;
    }
    u32 value = iSubresource;
    nFaceOut = value % texture.nFaces;
    value /= texture.nFaces;
    nLayerOut = value % texture.nLayers;
    value /= texture.nLayers;
    nFrameOut = value % texture.nFrames;
    value /= texture.nFrames;
    nMipLevelOut = value;
    return nMipLevelOut < texture.nMipLevels;
}

CYPHER_NODISCARD bool_t ExpectedSubresourceLayout(
    const cooked_texture_desc_t &texture,
    u32 nMipLevel,
    u32 &nWidthOut,
    u32 &nHeightOut,
    u32 &nDepthOut,
    u32 &cbRowPitchOut,
    u32 &cbSlicePitchOut,
    u64 &cbDataOut ) noexcept
{
    cooked_texture_format_layout_t layout{};
    if ( nMipLevel >= texture.nMipLevels ||
         !CookedTexture_FormatLayout( texture.storageFormat, &layout ) ) {
        return CY_FALSE;
    }
    nWidthOut = texture.nWidth >> nMipLevel;
    nHeightOut = texture.nHeight >> nMipLevel;
    nDepthOut = texture.nDepth >> nMipLevel;
    if ( nWidthOut == 0u ) nWidthOut = 1u;
    if ( nHeightOut == 0u ) nHeightOut = 1u;
    if ( nDepthOut == 0u ) nDepthOut = 1u;

    const u64 nBlocksX = DivideRoundUp( nWidthOut, layout.nBlockWidth );
    const u64 nBlocksY = DivideRoundUp( nHeightOut, layout.nBlockHeight );
    const u64 nBlocksZ = DivideRoundUp( nDepthOut, layout.nBlockDepth );
    u64 cbRow = 0u;
    u64 cbSlice = 0u;
    if ( !CheckedMultiplyU64( nBlocksX, layout.cbBlock, cbRow ) ||
         !CheckedMultiplyU64( cbRow, nBlocksY, cbSlice ) ||
         !CheckedMultiplyU64( cbSlice, nBlocksZ, cbDataOut ) ||
         cbRow > CY_U32_MAX || cbSlice > CY_U32_MAX || cbDataOut == 0u ||
         cbDataOut > CY_COOKED_TEXTURE_MAX_DATA_SIZE ) {
        return CY_FALSE;
    }
    cbRowPitchOut = static_cast<u32>( cbRow );
    cbSlicePitchOut = static_cast<u32>( cbSlice );
    return CY_TRUE;
}

CYPHER_NODISCARD cooked_texture_subresource_desc_t MakeSubresourceDescriptor(
    const cooked_texture_subresource_source_t &source,
    usize iSubresource ) noexcept
{
    return {
        source.nMipLevel,
        source.nFrame,
        source.nLayer,
        source.nFace,
        source.nWidth,
        source.nHeight,
        source.nDepth,
        source.cbRowPitch,
        source.cbSlicePitch,
        static_cast<u32>( iSubresource + 1u ),
        0u,
        0u,
        source.pixels.cbSize,
        0u
    };
}

CYPHER_NODISCARD cooked_texture_status_t ValidateSubresource(
    const cooked_texture_desc_t &texture,
    const cooked_texture_subresource_desc_t &entry,
    usize iSubresource,
    u64 &cbTotal,
    u64 &cbResident ) noexcept
{
    u32 nMip = 0u;
    u32 nFrame = 0u;
    u32 nLayer = 0u;
    u32 nFace = 0u;
    u32 nWidth = 0u;
    u32 nHeight = 0u;
    u32 nDepth = 0u;
    u32 cbRow = 0u;
    u32 cbSlice = 0u;
    u64 cbData = 0u;
    const bool_t bExpected = ExpectedCoordinates(
        texture,
        static_cast<u32>( iSubresource ),
        nMip,
        nFrame,
        nLayer,
        nFace ) && ExpectedSubresourceLayout(
            texture,
            nMip,
            nWidth,
            nHeight,
            nDepth,
            cbRow,
            cbSlice,
            cbData );
    if ( !bExpected || entry.nMipLevel != nMip ||
         entry.nFrame != nFrame || entry.nLayer != nLayer ||
         entry.nFace != nFace || entry.nWidth != nWidth ||
         entry.nHeight != nHeight || entry.nDepth != nDepth ||
         entry.cbRowPitch != cbRow || entry.cbSlicePitch != cbSlice ||
         entry.iDataChunk != iSubresource + 1u || entry.flags != 0u ||
         entry.nReserved != 0u || entry.cbData != cbData ||
         entry.nReserved64 != 0u ||
         cbData > CY_COOKED_TEXTURE_MAX_DATA_SIZE - cbTotal ) {
        return cooked_texture_status_t::INVALID_SUBRESOURCE_LAYOUT;
    }
    cbTotal += cbData;
    if ( iSubresource >= texture.iResidentFirstSubresource ) {
        cbResident += cbData;
    }
    return cooked_texture_status_t::OK;
}

CYPHER_NODISCARD cooked_texture_status_t ValidateSubresources(
    cooked_texture_desc_t &texture,
    span_t<const cooked_texture_subresource_desc_t> subresources,
    usize *pInvalidSubresource ) noexcept
{
    if ( pInvalidSubresource != nullptr ) {
        *pInvalidSubresource = CY_INVALID_SIZE;
    }
    if ( !Span_IsValid( subresources ) ||
         subresources.nCount != texture.nSubresources ) {
        return cooked_texture_status_t::INVALID_SUBRESOURCE_LAYOUT;
    }
    u64 cbTotal = 0u;
    u64 cbResident = 0u;
    for ( usize i = 0u; i < subresources.nCount; ++i ) {
        const cooked_texture_status_t status = ValidateSubresource(
            texture,
            subresources.pData[i],
            i,
            cbTotal,
            cbResident );
        if ( status != cooked_texture_status_t::OK ) {
            if ( pInvalidSubresource != nullptr ) {
                *pInvalidSubresource = i;
            }
            return status;
        }
    }
    texture.cbData = cbTotal;
    texture.cbResidentData = cbResident;
    return cooked_texture_status_t::OK;
}

CYPHER_NODISCARD bool_t WriteMetadataHeaderV2(
    byte_writer_t &writer,
    const cooked_texture_desc_t &texture ) noexcept
{
    return ByteWriter_WriteU32( &writer, CY_COOKED_TEXTURE_METADATA_MAGIC ) &&
           ByteWriter_WriteU32( &writer, CY_COOKED_TEXTURE_METADATA_VERSION_V2 ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( CY_COOKED_TEXTURE_METADATA_HEADER_SIZE ) ) &&
           ByteWriter_WriteU32( &writer, static_cast<u32>( texture.dimension ) ) &&
           ByteWriter_WriteU32( &writer, static_cast<u32>( texture.storageFormat ) ) &&
           ByteWriter_WriteU32( &writer, static_cast<u32>( texture.usage ) ) &&
           ByteWriter_WriteU32( &writer, static_cast<u32>( texture.colorSpace ) ) &&
           ByteWriter_WriteU32( &writer, static_cast<u32>( texture.alphaMode ) ) &&
           ByteWriter_WriteU32( &writer, static_cast<u32>( texture.target ) ) &&
           ByteWriter_WriteU32( &writer, static_cast<u32>( texture.residency ) ) &&
           ByteWriter_WriteU32( &writer, texture.flags ) &&
           ByteWriter_WriteU32( &writer, texture.nWidth ) &&
           ByteWriter_WriteU32( &writer, texture.nHeight ) &&
           ByteWriter_WriteU32( &writer, texture.nDepth ) &&
           ByteWriter_WriteU32( &writer, texture.nLayers ) &&
           ByteWriter_WriteU32( &writer, texture.nFaces ) &&
           ByteWriter_WriteU32( &writer, texture.nFrames ) &&
           ByteWriter_WriteU32( &writer, texture.nMipLevels ) &&
           ByteWriter_WriteU32( &writer, texture.nSubresources ) &&
           ByteWriter_WriteU32( &writer, texture.nResidentMipLevels ) &&
           ByteWriter_WriteU32(
               &writer,
               texture.iResidentFirstSubresource ) &&
           ByteWriter_WriteU32( &writer, texture.nResidentSubresources ) &&
           ByteWriter_WriteU32( &writer, texture.nStreamingPriority ) &&
           ByteWriter_WriteF32( &writer, texture.alphaCutoff ) &&
           ByteWriter_WriteU64( &writer, texture.cbResidentData ) &&
           ByteWriter_WriteU64( &writer, texture.cbData ) &&
           ByteWriter_WriteU64( &writer, 0u ) &&
           ByteWriter_WriteU64( &writer, 0u );
}

CYPHER_NODISCARD bool_t WriteSubresourceDescriptor(
    byte_writer_t &writer,
    const cooked_texture_subresource_desc_t &entry ) noexcept
{
    return ByteWriter_WriteU32( &writer, entry.nMipLevel ) &&
           ByteWriter_WriteU32( &writer, entry.nFrame ) &&
           ByteWriter_WriteU32( &writer, entry.nLayer ) &&
           ByteWriter_WriteU32( &writer, entry.nFace ) &&
           ByteWriter_WriteU32( &writer, entry.nWidth ) &&
           ByteWriter_WriteU32( &writer, entry.nHeight ) &&
           ByteWriter_WriteU32( &writer, entry.nDepth ) &&
           ByteWriter_WriteU32( &writer, entry.cbRowPitch ) &&
           ByteWriter_WriteU32( &writer, entry.cbSlicePitch ) &&
           ByteWriter_WriteU32( &writer, entry.iDataChunk ) &&
           ByteWriter_WriteU32( &writer, entry.flags ) &&
           ByteWriter_WriteU32( &writer, entry.nReserved ) &&
           ByteWriter_WriteU64( &writer, entry.cbData ) &&
           ByteWriter_WriteU64( &writer, entry.nReserved64 );
}

CYPHER_NODISCARD bool_t ReadMetadataHeaderV1(
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
         !ByteReader_ReadU32( &reader, &reserved1 ) ||
         magic != CY_COOKED_TEXTURE_METADATA_MAGIC ||
         version != CY_COOKED_TEXTURE_METADATA_VERSION_V1 ||
         cbHeader != CY_COOKED_TEXTURE_METADATA_HEADER_SIZE_V1 ||
         usage > static_cast<u32>( render_texture_usage_t::DATA ) ||
         colorSpace >
             static_cast<u32>( render_texture_color_space_t::LINEAR ) ||
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
    metadata.texture.storageFormat = LegacyStorageFormat(
        metadata.texture.pixelFormat );
    cooked_texture_format_layout_t layout{};
    if ( !CookedTexture_FormatLayout(
             metadata.texture.storageFormat,
             &layout ) ) {
        return CY_FALSE;
    }
    metadata.texture.alphaMode = ResolveAlphaMode( metadata.texture, layout );
    metadata.texture.alphaCutoff = 0.5f;
    metadata.texture.target = cooked_texture_target_t::PORTABLE;
    metadata.texture.nFrames = 1u;
    metadata.texture.nSubresources = metadata.texture.nMipLevels;
    metadata.texture.residency = cooked_texture_residency_t::FULLY_RESIDENT;
    metadata.texture.nResidentMipLevels = metadata.texture.nMipLevels;
    metadata.texture.iResidentFirstSubresource = 0u;
    metadata.texture.nResidentSubresources = metadata.texture.nMipLevels;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ReadMetadataHeaderV2(
    byte_reader_t &reader,
    cooked_texture_metadata_t &metadata ) noexcept
{
    u32 magic = 0u;
    u32 version = 0u;
    u32 cbHeader = 0u;
    u32 dimension = 0u;
    u32 storageFormat = 0u;
    u32 usage = 0u;
    u32 colorSpace = 0u;
    u32 alphaMode = 0u;
    u32 target = 0u;
    u32 residency = 0u;
    u64 reserved64A = 0u;
    u64 reserved64B = 0u;
    if ( !ByteReader_ReadU32( &reader, &magic ) ||
         !ByteReader_ReadU32( &reader, &version ) ||
         !ByteReader_ReadU32( &reader, &cbHeader ) ||
         !ByteReader_ReadU32( &reader, &dimension ) ||
         !ByteReader_ReadU32( &reader, &storageFormat ) ||
         !ByteReader_ReadU32( &reader, &usage ) ||
         !ByteReader_ReadU32( &reader, &colorSpace ) ||
         !ByteReader_ReadU32( &reader, &alphaMode ) ||
         !ByteReader_ReadU32( &reader, &target ) ||
         !ByteReader_ReadU32( &reader, &residency ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.flags ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nWidth ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nHeight ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nDepth ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nLayers ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nFaces ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nFrames ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nMipLevels ) ||
         !ByteReader_ReadU32( &reader, &metadata.texture.nSubresources ) ||
         !ByteReader_ReadU32(
             &reader,
             &metadata.texture.nResidentMipLevels ) ||
         !ByteReader_ReadU32(
             &reader,
             &metadata.texture.iResidentFirstSubresource ) ||
         !ByteReader_ReadU32(
             &reader,
             &metadata.texture.nResidentSubresources ) ||
         !ByteReader_ReadU32(
             &reader,
             &metadata.texture.nStreamingPriority ) ||
         !ByteReader_ReadF32( &reader, &metadata.texture.alphaCutoff ) ||
         !ByteReader_ReadU64( &reader, &metadata.texture.cbResidentData ) ||
         !ByteReader_ReadU64( &reader, &metadata.texture.cbData ) ||
         !ByteReader_ReadU64( &reader, &reserved64A ) ||
         !ByteReader_ReadU64( &reader, &reserved64B ) ||
         magic != CY_COOKED_TEXTURE_METADATA_MAGIC ||
         version != CY_COOKED_TEXTURE_METADATA_VERSION_V2 ||
         cbHeader != CY_COOKED_TEXTURE_METADATA_HEADER_SIZE ||
         storageFormat > CY_U16_MAX ||
         usage > static_cast<u32>( render_texture_usage_t::DATA ) ||
         colorSpace >
             static_cast<u32>( render_texture_color_space_t::LINEAR ) ||
         reserved64A != 0u || reserved64B != 0u ) {
        return CY_FALSE;
    }
    metadata.texture.dimension =
        static_cast<render_texture_dimension_t>( dimension );
    metadata.texture.storageFormat = static_cast<render_format_t>( storageFormat );
    metadata.texture.pixelFormat = LegacyPixelFormat(
        metadata.texture.storageFormat );
    metadata.texture.usage = static_cast<render_texture_usage_t>( usage );
    metadata.texture.colorSpace =
        static_cast<render_texture_color_space_t>( colorSpace );
    metadata.texture.alphaMode =
        static_cast<cooked_texture_alpha_mode_t>( alphaMode );
    metadata.texture.target = static_cast<cooked_texture_target_t>( target );
    metadata.texture.residency =
        static_cast<cooked_texture_residency_t>( residency );
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ReadMipDescriptorV1(
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

CYPHER_NODISCARD bool_t ReadSubresourceDescriptor(
    byte_reader_t &reader,
    cooked_texture_subresource_desc_t &entry ) noexcept
{
    return ByteReader_ReadU32( &reader, &entry.nMipLevel ) &&
           ByteReader_ReadU32( &reader, &entry.nFrame ) &&
           ByteReader_ReadU32( &reader, &entry.nLayer ) &&
           ByteReader_ReadU32( &reader, &entry.nFace ) &&
           ByteReader_ReadU32( &reader, &entry.nWidth ) &&
           ByteReader_ReadU32( &reader, &entry.nHeight ) &&
           ByteReader_ReadU32( &reader, &entry.nDepth ) &&
           ByteReader_ReadU32( &reader, &entry.cbRowPitch ) &&
           ByteReader_ReadU32( &reader, &entry.cbSlicePitch ) &&
           ByteReader_ReadU32( &reader, &entry.iDataChunk ) &&
           ByteReader_ReadU32( &reader, &entry.flags ) &&
           ByteReader_ReadU32( &reader, &entry.nReserved ) &&
           ByteReader_ReadU64( &reader, &entry.cbData ) &&
           ByteReader_ReadU64( &reader, &entry.nReserved64 );
}

CYPHER_NODISCARD cooked_texture_status_t ReadSubresourceEntry(
    byte_reader_t &reader,
    bool_t bVersion1,
    const cooked_texture_desc_t &texture,
    usize iSubresource,
    cooked_texture_subresource_desc_t &entryOut ) noexcept
{
    if ( !bVersion1 ) {
        return ReadSubresourceDescriptor( reader, entryOut )
            ? cooked_texture_status_t::OK
            : cooked_texture_status_t::INVALID_METADATA;
    }

    cooked_texture_mip_desc_t mip{};
    if ( !ReadMipDescriptorV1( reader, mip ) ) {
        return cooked_texture_status_t::INVALID_METADATA;
    }
    u32 expectedWidth = 0u;
    u32 expectedHeight = 0u;
    u32 expectedDepth = 0u;
    u32 expectedRow = 0u;
    u32 cbSlice = 0u;
    u64 expectedData = 0u;
    if ( !ExpectedSubresourceLayout(
             texture,
             static_cast<u32>( iSubresource ),
             expectedWidth,
             expectedHeight,
             expectedDepth,
             expectedRow,
             cbSlice,
             expectedData ) ) {
        return cooked_texture_status_t::INVALID_MIP_CHAIN;
    }
    entryOut = {
        mip.nLevel, 0u, 0u, 0u,
        mip.nWidth, mip.nHeight, mip.nDepth,
        mip.cbRowPitch, cbSlice, mip.iDataChunk,
        0u, 0u, mip.cbData, 0u
    };
    return cooked_texture_status_t::OK;
}

CYPHER_NODISCARD bool_t ReadChunkAt(
    binary_block_t file,
    u32 iChunk,
    cooked_chunk_desc_t &chunkOut ) noexcept
{
    const usize iOffset = CY_COOKED_RESOURCE_HEADER_SIZE +
        static_cast<usize>( iChunk ) * CY_COOKED_RESOURCE_CHUNK_SIZE;
    if ( iOffset > file.cbSize ||
         CY_COOKED_RESOURCE_CHUNK_SIZE > file.cbSize - iOffset ) {
        return CY_FALSE;
    }
    byte_reader_t reader{};
    u32 codec = 0u;
    if ( !ByteReader_Init(
             &reader,
             { file.pData + iOffset, CY_COOKED_RESOURCE_CHUNK_SIZE },
             data_byte_order_t::LITTLE ) ||
         !ByteReader_ReadU32( &reader, &chunkOut.chunkType ) ||
         !ByteReader_ReadU32( &reader, &codec ) ||
         !ByteReader_ReadU32( &reader, &chunkOut.flags ) ||
         !ByteReader_ReadU32( &reader, &chunkOut.nAlignment ) ||
         !ByteReader_ReadU64( &reader, &chunkOut.iOffset ) ||
         !ByteReader_ReadU64( &reader, &chunkOut.cbStored ) ||
         !ByteReader_ReadU64( &reader, &chunkOut.cbDecoded ) ||
         !ByteReader_ReadU64( &reader, &chunkOut.nReserved ) ||
         !ByteReader_ReadU64( &reader, &chunkOut.contentHash.low ) ||
         !ByteReader_ReadU64( &reader, &chunkOut.contentHash.high ) ) {
        return CY_FALSE;
    }
    chunkOut.codec = static_cast<cooked_chunk_codec_t>( codec );
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ReadResourceHeader(
    byte_reader_t &reader,
    cooked_resource_header_t &header ) noexcept
{
    return ByteReader_ReadU32( &reader, &header.magic ) &&
           ByteReader_ReadU32( &reader, &header.nContainerVersion ) &&
           ByteReader_ReadU32( &reader, &header.cbHeader ) &&
           ByteReader_ReadU32( &reader, &header.resourceType ) &&
           ByteReader_ReadU32( &reader, &header.nResourceVersion ) &&
           ByteReader_ReadU32( &reader, &header.flags ) &&
           ByteReader_ReadU32( &reader, &header.nChunks ) &&
           ByteReader_ReadU32( &reader, &header.nReserved ) &&
           ByteReader_ReadU64( &reader, &header.cbFile ) &&
           ByteReader_ReadU64( &reader, &header.iChunkTable ) &&
           ByteReader_ReadU64( &reader, &header.sourceHash.low ) &&
           ByteReader_ReadU64( &reader, &header.sourceHash.high ) &&
           ByteReader_ReadU64( &reader, &header.contentHash.low ) &&
           ByteReader_ReadU64( &reader, &header.contentHash.high );
}

CYPHER_NODISCARD bool_t WriteResourceHeader(
    byte_writer_t &writer,
    const cooked_resource_header_t &header ) noexcept
{
    return ByteWriter_WriteU32( &writer, header.magic ) &&
           ByteWriter_WriteU32( &writer, header.nContainerVersion ) &&
           ByteWriter_WriteU32( &writer, header.cbHeader ) &&
           ByteWriter_WriteU32( &writer, header.resourceType ) &&
           ByteWriter_WriteU32( &writer, header.nResourceVersion ) &&
           ByteWriter_WriteU32( &writer, header.flags ) &&
           ByteWriter_WriteU32( &writer, header.nChunks ) &&
           ByteWriter_WriteU32( &writer, header.nReserved ) &&
           ByteWriter_WriteU64( &writer, header.cbFile ) &&
           ByteWriter_WriteU64( &writer, header.iChunkTable ) &&
           ByteWriter_WriteU64( &writer, header.sourceHash.low ) &&
           ByteWriter_WriteU64( &writer, header.sourceHash.high ) &&
           ByteWriter_WriteU64( &writer, header.contentHash.low ) &&
           ByteWriter_WriteU64( &writer, header.contentHash.high );
}

CYPHER_NODISCARD bool_t WriteResourceChunk(
    byte_writer_t &writer,
    const cooked_chunk_desc_t &chunk ) noexcept
{
    return ByteWriter_WriteU32( &writer, chunk.chunkType ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( chunk.codec ) ) &&
           ByteWriter_WriteU32( &writer, chunk.flags ) &&
           ByteWriter_WriteU32( &writer, chunk.nAlignment ) &&
           ByteWriter_WriteU64( &writer, chunk.iOffset ) &&
           ByteWriter_WriteU64( &writer, chunk.cbStored ) &&
           ByteWriter_WriteU64( &writer, chunk.cbDecoded ) &&
           ByteWriter_WriteU64( &writer, chunk.nReserved ) &&
           ByteWriter_WriteU64( &writer, chunk.contentHash.low ) &&
           ByteWriter_WriteU64( &writer, chunk.contentHash.high );
}

CYPHER_NODISCARD bool_t IsResourceCodecValid(
    cooked_chunk_codec_t codec ) noexcept
{
    return codec == cooked_chunk_codec_t::NONE ||
           codec == cooked_chunk_codec_t::LZ4 ||
           codec == cooked_chunk_codec_t::ZSTD;
}

CYPHER_NODISCARD cooked_resource_status_t ValidateResourceHeader(
    const cooked_resource_header_t &header ) noexcept
{
    constexpr flags32_t knownFlags =
        COOKED_RESOURCE_FLAG_HAS_SOURCE_HASH |
        COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH;
    if ( header.magic != CY_COOKED_RESOURCE_MAGIC ) {
        return cooked_resource_status_t::INVALID_MAGIC;
    }
    if ( header.nContainerVersion !=
         CY_COOKED_RESOURCE_CONTAINER_VERSION ) {
        return cooked_resource_status_t::VERSION_MISMATCH;
    }
    if ( header.cbHeader != CY_COOKED_RESOURCE_HEADER_SIZE ||
         header.iChunkTable != CY_COOKED_RESOURCE_HEADER_SIZE ||
         header.nReserved != 0u ) {
        return cooked_resource_status_t::INVALID_HEADER;
    }
    if ( header.resourceType == CY_INVALID_FOURCC ||
         header.nResourceVersion == 0u ) {
        return cooked_resource_status_t::INVALID_RESOURCE_TYPE;
    }
    if ( ( header.flags & ~knownFlags ) != 0u ) {
        return cooked_resource_status_t::INVALID_FLAGS;
    }
    if ( header.nChunks == 0u ||
         header.nChunks > CY_COOKED_RESOURCE_MAX_CHUNKS ) {
        return cooked_resource_status_t::CHUNK_LIMIT_EXCEEDED;
    }
    const bool_t bHasSourceHash =
        ( header.flags & COOKED_RESOURCE_FLAG_HAS_SOURCE_HASH ) != 0u;
    const bool_t bHasContentHash =
        ( header.flags & COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH ) != 0u;
    if ( bHasSourceHash != ContentHash_IsValid( header.sourceHash ) ||
         bHasContentHash != ContentHash_IsValid( header.contentHash ) ) {
        return cooked_resource_status_t::INVALID_HEADER;
    }
    const usize cbPrefix = CookedResource_PrefixSize( header.nChunks );
    if ( cbPrefix == 0u || header.cbFile < cbPrefix ) {
        return cooked_resource_status_t::INVALID_HEADER;
    }
    return cooked_resource_status_t::OK;
}

CYPHER_NODISCARD cooked_resource_status_t ValidateResourceChunk(
    const cooked_chunk_desc_t &chunk ) noexcept
{
    constexpr flags32_t knownFlags =
        COOKED_CHUNK_FLAG_COMPRESSED |
        COOKED_CHUNK_FLAG_OPTIONAL |
        COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    if ( chunk.chunkType == CY_INVALID_FOURCC ||
         !IsResourceCodecValid( chunk.codec ) || chunk.nReserved != 0u ||
         chunk.cbStored == 0u || chunk.cbDecoded == 0u ||
         chunk.nAlignment == 0u ||
         chunk.nAlignment > CY_COOKED_RESOURCE_MAX_ALIGNMENT ||
         ( chunk.nAlignment & ( chunk.nAlignment - 1u ) ) != 0u ) {
        return cooked_resource_status_t::INVALID_CHUNK;
    }
    if ( ( chunk.flags & ~knownFlags ) != 0u ) {
        return cooked_resource_status_t::INVALID_FLAGS;
    }
    const bool_t bCompressed =
        ( chunk.flags & COOKED_CHUNK_FLAG_COMPRESSED ) != 0u;
    if ( ( chunk.codec == cooked_chunk_codec_t::NONE &&
           ( bCompressed || chunk.cbStored != chunk.cbDecoded ) ) ||
         ( chunk.codec != cooked_chunk_codec_t::NONE && !bCompressed ) ) {
        return cooked_resource_status_t::INVALID_CHUNK;
    }
    const bool_t bHasContentHash =
        ( chunk.flags & COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ) != 0u;
    if ( bHasContentHash != ContentHash_IsValid( chunk.contentHash ) ) {
        return cooked_resource_status_t::INVALID_CHUNK;
    }
    return cooked_resource_status_t::OK;
}

// CYTX may contain 1025 chunks. Validate the generic envelope directly from
// the serialized table so texture reads remain constant-stack and transactional.
CYPHER_NODISCARD cooked_resource_result_t ReadResourceLayoutStreaming(
    binary_block_t input,
    cooked_resource_header_t &headerOut ) noexcept
{
    cooked_resource_result_t result{};
    if ( !BinaryBlock_IsValid( input ) ) {
        result.status = cooked_resource_status_t::INVALID_ARGUMENT;
        return result;
    }
    result.cbRequired = CY_COOKED_RESOURCE_HEADER_SIZE;
    if ( input.cbSize < CY_COOKED_RESOURCE_HEADER_SIZE ) {
        result.status = cooked_resource_status_t::TRUNCATED_INPUT;
        return result;
    }

    byte_reader_t reader{};
    cooked_resource_header_t header{};
    if ( !ByteReader_Init( &reader, input, data_byte_order_t::LITTLE ) ||
         !ReadResourceHeader( reader, header ) ) {
        result.status = cooked_resource_status_t::TRUNCATED_INPUT;
        return result;
    }
    result.status = ValidateResourceHeader( header );
    result.cbRequired = CookedResource_PrefixSize( header.nChunks );
    if ( result.status != cooked_resource_status_t::OK ) {
        return result;
    }
    if ( header.cbFile != input.cbSize ) {
        result.status = cooked_resource_status_t::FILE_SIZE_MISMATCH;
        return result;
    }
    if ( input.cbSize < result.cbRequired ) {
        result.status = cooked_resource_status_t::TRUNCATED_INPUT;
        return result;
    }
    if ( header.nChunks > CY_COOKED_TEXTURE_MAX_SUBRESOURCES + 1u ) {
        result.status = cooked_resource_status_t::OUTPUT_TOO_SMALL;
        return result;
    }

    u64 iPreviousEnd = result.cbRequired;
    for ( u32 iChunk = 0u; iChunk < header.nChunks; ++iChunk ) {
        cooked_chunk_desc_t chunk{};
        if ( !ReadChunkAt( input, iChunk, chunk ) ) {
            result.status = cooked_resource_status_t::TRUNCATED_INPUT;
            result.iChunk = iChunk;
            return result;
        }
        result.status = ValidateResourceChunk( chunk );
        if ( result.status != cooked_resource_status_t::OK ) {
            result.iChunk = iChunk;
            return result;
        }
        if ( chunk.iOffset < iPreviousEnd ) {
            result.status = cooked_resource_status_t::INVALID_CHUNK_ORDER;
            result.iChunk = iChunk;
            return result;
        }
        if ( ( chunk.iOffset & ( chunk.nAlignment - 1u ) ) != 0u ||
             chunk.iOffset > header.cbFile ||
             chunk.cbStored > header.cbFile - chunk.iOffset ) {
            result.status = cooked_resource_status_t::INVALID_CHUNK;
            result.iChunk = iChunk;
            return result;
        }
        iPreviousEnd = chunk.iOffset + chunk.cbStored;
    }
    if ( ( header.flags & COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH ) != 0u &&
         !ContentHash_Equals(
             CookedResource_ComputeContentHash( input ),
             header.contentHash ) ) {
        result.status = cooked_resource_status_t::CONTENT_HASH_MISMATCH;
        result.iChunk = CY_INVALID_SIZE;
        return result;
    }
    headerOut = header;
    result.cbRead = result.cbRequired;
    return result;
}

CYPHER_NODISCARD cooked_texture_result_t WriteMetadataV2(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_subresource_desc_t> subresources,
    byte_span_t output ) noexcept
{
    cooked_texture_result_t result{};
    result.cbRequired = CookedTexture_MetadataSize(
        static_cast<u32>( subresources.nCount ) );
    if ( result.cbRequired == 0u ) {
        result.status = cooked_texture_status_t::SUBRESOURCE_LIMIT_EXCEEDED;
        return result;
    }
    if ( !Span_IsValid( subresources ) || !Span_IsValid( output ) ) {
        result.status = cooked_texture_status_t::INVALID_ARGUMENT;
        return result;
    }
    cooked_texture_desc_t validated{};
    result.status = NormalizeTexture( texture, CY_FALSE, validated );
    if ( result.status != cooked_texture_status_t::OK ) {
        return result;
    }
    result.status = ValidateSubresources(
        validated,
        subresources,
        &result.iSubresource );
    if ( result.status != cooked_texture_status_t::OK ||
         validated.cbData != texture.cbData ||
         validated.cbResidentData != texture.cbResidentData ) {
        if ( result.status == cooked_texture_status_t::OK ) {
            result.status = cooked_texture_status_t::INVALID_SUBRESOURCE_LAYOUT;
        }
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
             subresources.pData,
             subresources.nCount * sizeof( cooked_texture_subresource_desc_t ) ) ) {
        result.status = cooked_texture_status_t::INVALID_ARGUMENT;
        return result;
    }
    byte_writer_t writer{};
    if ( !ByteWriter_Init( &writer, output, data_byte_order_t::LITTLE ) ||
         !WriteMetadataHeaderV2( writer, validated ) ) {
        result.status = cooked_texture_status_t::OUTPUT_TOO_SMALL;
        return result;
    }
    for ( usize i = 0u; i < subresources.nCount; ++i ) {
        if ( !WriteSubresourceDescriptor( writer, subresources.pData[i] ) ) {
            result.status = cooked_texture_status_t::OUTPUT_TOO_SMALL;
            result.iSubresource = i;
            return result;
        }
    }
    result.cbWritten = ByteWriter_BytesWritten( &writer );
    return result;
}

CYPHER_NODISCARD bool_t PrepareCanonicalLayout(
    cooked_texture_desc_t &texture,
    span_t<const cooked_texture_subresource_source_t> sources,
    usize &cbFileOut,
    usize *pInvalidSubresource ) noexcept
{
    if ( pInvalidSubresource != nullptr ) {
        *pInvalidSubresource = CY_INVALID_SIZE;
    }
    const u32 nChunks = static_cast<u32>( sources.nCount + 1u );
    usize iOffset = CookedResource_PrefixSize( nChunks );
    if ( iOffset == 0u ||
         !Cy_AlignUpChecked(
             iOffset,
             CY_COOKED_TEXTURE_METADATA_ALIGNMENT,
             iOffset ) ) {
        return CY_FALSE;
    }
    const usize cbMetadata = CookedTexture_MetadataSize(
        static_cast<u32>( sources.nCount ) );
    if ( cbMetadata == 0u ) {
        return CY_FALSE;
    }
    if ( !CheckedAdd( iOffset, cbMetadata, iOffset ) ) {
        return CY_FALSE;
    }

    u64 cbTotal = 0u;
    u64 cbResident = 0u;
    for ( usize i = 0u; i < sources.nCount; ++i ) {
        const cooked_texture_subresource_source_t &source = sources.pData[i];
        const cooked_texture_subresource_desc_t entry =
            MakeSubresourceDescriptor( source, i );
        if ( ValidateSubresource(
                 texture,
                 entry,
                 i,
                 cbTotal,
                 cbResident ) != cooked_texture_status_t::OK ) {
            if ( pInvalidSubresource != nullptr ) {
                *pInvalidSubresource = i;
            }
            return CY_FALSE;
        }
        if ( !Cy_AlignUpChecked(
                 iOffset,
                 CY_COOKED_TEXTURE_DATA_ALIGNMENT,
                 iOffset ) ) {
            return CY_FALSE;
        }
        if ( !CheckedAdd( iOffset, source.pixels.cbSize, iOffset ) ) {
            return CY_FALSE;
        }
    }
    texture.cbData = cbTotal;
    texture.cbResidentData = cbResident;
    cbFileOut = iOffset;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t WriteMetadataV2FromSources(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_subresource_source_t> sources,
    byte_span_t output ) noexcept
{
    const usize cbMetadata = CookedTexture_MetadataSize(
        static_cast<u32>( sources.nCount ) );
    byte_writer_t writer{};
    if ( cbMetadata == 0u || output.nCount < cbMetadata ||
         !ByteWriter_Init( &writer, output, data_byte_order_t::LITTLE ) ||
         !WriteMetadataHeaderV2( writer, texture ) ) {
        return CY_FALSE;
    }
    for ( usize i = 0u; i < sources.nCount; ++i ) {
        const cooked_texture_subresource_desc_t entry =
            MakeSubresourceDescriptor( sources.pData[i], i );
        if ( !WriteSubresourceDescriptor( writer, entry ) ) {
            return CY_FALSE;
        }
    }
    return ByteWriter_BytesWritten( &writer ) == cbMetadata;
}

CYPHER_NODISCARD bool_t WriteTextureChunkTable(
    const cooked_resource_header_t &header,
    const cooked_chunk_desc_t &metadataChunk,
    span_t<const cooked_texture_subresource_source_t> sources,
    byte_span_t output ) noexcept
{
    const usize cbPrefix = CookedResource_PrefixSize( header.nChunks );
    byte_writer_t writer{};
    if ( cbPrefix == 0u || output.nCount < cbPrefix ||
         !ByteWriter_Init(
             &writer,
             { output.pData, cbPrefix },
             data_byte_order_t::LITTLE ) ||
         !WriteResourceHeader( writer, header ) ||
         !WriteResourceChunk( writer, metadataChunk ) ) {
        return CY_FALSE;
    }

    usize iOffset = static_cast<usize>(
        metadataChunk.iOffset + metadataChunk.cbStored );
    for ( usize i = 0u; i < sources.nCount; ++i ) {
        if ( !Cy_AlignUpChecked(
                 iOffset,
                 CY_COOKED_TEXTURE_DATA_ALIGNMENT,
                 iOffset ) ) {
            return CY_FALSE;
        }
        const cooked_texture_subresource_source_t &source = sources.pData[i];
        cooked_chunk_desc_t chunk{};
        chunk.chunkType = CY_COOKED_TEXTURE_DATA_CHUNK;
        chunk.nAlignment = CY_COOKED_TEXTURE_DATA_ALIGNMENT;
        chunk.iOffset = iOffset;
        chunk.cbStored = source.pixels.cbSize;
        chunk.cbDecoded = source.pixels.cbSize;
        chunk.flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
        chunk.contentHash = ContentHash_Data( source.pixels );
        if ( !ContentHash_IsValid( chunk.contentHash ) ||
             !WriteResourceChunk( writer, chunk ) ||
             !CheckedAdd( iOffset, source.pixels.cbSize, iOffset ) ) {
            return CY_FALSE;
        }
    }
    return ByteWriter_BytesWritten( &writer ) == cbPrefix;
}

CYPHER_NODISCARD bool_t RewriteResourceHeader(
    const cooked_resource_header_t &header,
    byte_span_t output ) noexcept
{
    byte_writer_t writer{};
    return output.nCount >= CY_COOKED_RESOURCE_HEADER_SIZE &&
           ByteWriter_Init(
               &writer,
               { output.pData, CY_COOKED_RESOURCE_HEADER_SIZE },
               data_byte_order_t::LITTLE ) &&
           WriteResourceHeader( writer, header ) &&
           ByteWriter_BytesWritten( &writer ) == CY_COOKED_RESOURCE_HEADER_SIZE;
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
        case render_texture_pixel_format_t::UNKNOWN:
            break;
    }
    return 0u;
}

bool_t CookedTexture_FormatLayout(
    render_format_t storageFormat,
    cooked_texture_format_layout_t *pLayoutOut ) noexcept
{
    if ( pLayoutOut == nullptr ) {
        return CY_FALSE;
    }
    cooked_texture_format_layout_t layout{ 1u, 1u, 1u, 0u, CY_FALSE,
                                            CY_FALSE, CY_FALSE };
    switch ( storageFormat ) {
        case render_format_t::R8_UNORM:
        case render_format_t::R8_SNORM:
        case render_format_t::R8_UINT:
        case render_format_t::R8_SINT: layout.cbBlock = 1u; break;
        case render_format_t::RG8_UNORM:
        case render_format_t::RG8_SNORM:
        case render_format_t::RG8_UINT:
        case render_format_t::RG8_SINT:
        case render_format_t::RGB565_UNORM: layout.cbBlock = 2u; break;
        case render_format_t::RGB8_UNORM:
        case render_format_t::RGB8_SNORM:
        case render_format_t::RGB8_UINT:
        case render_format_t::RGB8_SINT:
        case render_format_t::RGB8_SRGB:
            layout.cbBlock = 3u;
            layout.bSrgb = storageFormat == render_format_t::RGB8_SRGB;
            break;
        case render_format_t::RGBA8_UNORM:
        case render_format_t::RGBA8_SNORM:
        case render_format_t::RGBA8_UINT:
        case render_format_t::RGBA8_SINT:
        case render_format_t::RGBA8_SRGB:
        case render_format_t::BGRA8_UNORM:
        case render_format_t::BGRA8_SRGB:
        case render_format_t::RGB10A2_UNORM:
        case render_format_t::RGB10A2_UINT:
            layout.cbBlock = 4u;
            layout.bHasAlpha = CY_TRUE;
            layout.bSrgb = storageFormat == render_format_t::RGBA8_SRGB ||
                           storageFormat == render_format_t::BGRA8_SRGB;
            break;
        case render_format_t::RGBA4_UNORM:
        case render_format_t::RGB5A1_UNORM:
            layout.cbBlock = 2u;
            layout.bHasAlpha = CY_TRUE;
            break;
        case render_format_t::R16_UNORM:
        case render_format_t::R16_SNORM:
        case render_format_t::R16_UINT:
        case render_format_t::R16_SINT:
        case render_format_t::R16_FLOAT:
        case render_format_t::D16_UNORM: layout.cbBlock = 2u; break;
        case render_format_t::RG16_UNORM:
        case render_format_t::RG16_SNORM:
        case render_format_t::RG16_UINT:
        case render_format_t::RG16_SINT:
        case render_format_t::RG16_FLOAT:
        case render_format_t::R32_UINT:
        case render_format_t::R32_SINT:
        case render_format_t::R32_FLOAT:
        case render_format_t::R11G11B10_UFLOAT:
        case render_format_t::RGB9E5_UFLOAT:
        case render_format_t::D24_UNORM_S8_UINT:
        case render_format_t::D32_FLOAT: layout.cbBlock = 4u; break;
        case render_format_t::RGB16_UNORM:
        case render_format_t::RGB16_SNORM:
        case render_format_t::RGB16_UINT:
        case render_format_t::RGB16_SINT:
        case render_format_t::RGB16_FLOAT: layout.cbBlock = 6u; break;
        case render_format_t::RGBA16_UNORM:
        case render_format_t::RGBA16_SNORM:
        case render_format_t::RGBA16_UINT:
        case render_format_t::RGBA16_SINT:
        case render_format_t::RGBA16_FLOAT:
        case render_format_t::RG32_UINT:
        case render_format_t::RG32_SINT:
        case render_format_t::RG32_FLOAT:
        case render_format_t::D32_FLOAT_S8_UINT:
            layout.cbBlock = 8u;
            layout.bHasAlpha = storageFormat >= render_format_t::RGBA16_UNORM &&
                               storageFormat <= render_format_t::RGBA16_FLOAT;
            break;
        case render_format_t::RGB32_UINT:
        case render_format_t::RGB32_SINT:
        case render_format_t::RGB32_FLOAT: layout.cbBlock = 12u; break;
        case render_format_t::RGBA32_UINT:
        case render_format_t::RGBA32_SINT:
        case render_format_t::RGBA32_FLOAT:
            layout.cbBlock = 16u;
            layout.bHasAlpha = CY_TRUE;
            break;
        case render_format_t::S8_UINT: layout.cbBlock = 1u; break;

        case render_format_t::BC1_RGB_UNORM:
        case render_format_t::BC1_RGB_SRGB:
        case render_format_t::BC1_RGBA_UNORM:
        case render_format_t::BC1_RGBA_SRGB:
        case render_format_t::BC4_UNORM:
        case render_format_t::BC4_SNORM:
        case render_format_t::ETC2_RGB8_UNORM:
        case render_format_t::ETC2_RGB8_SRGB:
        case render_format_t::ETC2_RGB8A1_UNORM:
        case render_format_t::ETC2_RGB8A1_SRGB:
        case render_format_t::EAC_R11_UNORM:
        case render_format_t::EAC_R11_SNORM:
            layout = { 4u, 4u, 1u, 8u, CY_TRUE, CY_FALSE, CY_FALSE };
            layout.bSrgb = storageFormat == render_format_t::BC1_RGB_SRGB ||
                           storageFormat == render_format_t::BC1_RGBA_SRGB ||
                           storageFormat == render_format_t::ETC2_RGB8_SRGB ||
                           storageFormat == render_format_t::ETC2_RGB8A1_SRGB;
            layout.bHasAlpha = storageFormat == render_format_t::BC1_RGBA_UNORM ||
                               storageFormat == render_format_t::BC1_RGBA_SRGB ||
                               storageFormat == render_format_t::ETC2_RGB8A1_UNORM ||
                               storageFormat == render_format_t::ETC2_RGB8A1_SRGB;
            break;
        case render_format_t::BC2_UNORM:
        case render_format_t::BC2_SRGB:
        case render_format_t::BC3_UNORM:
        case render_format_t::BC3_SRGB:
        case render_format_t::BC5_UNORM:
        case render_format_t::BC5_SNORM:
        case render_format_t::BC6H_UFLOAT:
        case render_format_t::BC6H_SFLOAT:
        case render_format_t::BC7_UNORM:
        case render_format_t::BC7_SRGB:
        case render_format_t::ETC2_RGBA8_UNORM:
        case render_format_t::ETC2_RGBA8_SRGB:
        case render_format_t::EAC_RG11_UNORM:
        case render_format_t::EAC_RG11_SNORM:
            layout = { 4u, 4u, 1u, 16u, CY_TRUE, CY_FALSE, CY_FALSE };
            layout.bSrgb = storageFormat == render_format_t::BC2_SRGB ||
                           storageFormat == render_format_t::BC3_SRGB ||
                           storageFormat == render_format_t::BC7_SRGB ||
                           storageFormat == render_format_t::ETC2_RGBA8_SRGB;
            layout.bHasAlpha = storageFormat == render_format_t::BC2_UNORM ||
                               storageFormat == render_format_t::BC2_SRGB ||
                               storageFormat == render_format_t::BC3_UNORM ||
                               storageFormat == render_format_t::BC3_SRGB ||
                               storageFormat == render_format_t::BC7_UNORM ||
                               storageFormat == render_format_t::BC7_SRGB ||
                               storageFormat == render_format_t::ETC2_RGBA8_UNORM ||
                               storageFormat == render_format_t::ETC2_RGBA8_SRGB;
            break;

        case render_format_t::ASTC_4X4_UNORM:
        case render_format_t::ASTC_4X4_SRGB:
        case render_format_t::ASTC_5X4_UNORM:
        case render_format_t::ASTC_5X4_SRGB:
        case render_format_t::ASTC_5X5_UNORM:
        case render_format_t::ASTC_5X5_SRGB:
        case render_format_t::ASTC_6X5_UNORM:
        case render_format_t::ASTC_6X5_SRGB:
        case render_format_t::ASTC_6X6_UNORM:
        case render_format_t::ASTC_6X6_SRGB:
        case render_format_t::ASTC_8X5_UNORM:
        case render_format_t::ASTC_8X5_SRGB:
        case render_format_t::ASTC_8X6_UNORM:
        case render_format_t::ASTC_8X6_SRGB:
        case render_format_t::ASTC_8X8_UNORM:
        case render_format_t::ASTC_8X8_SRGB:
        case render_format_t::ASTC_10X5_UNORM:
        case render_format_t::ASTC_10X5_SRGB:
        case render_format_t::ASTC_10X6_UNORM:
        case render_format_t::ASTC_10X6_SRGB:
        case render_format_t::ASTC_10X8_UNORM:
        case render_format_t::ASTC_10X8_SRGB:
        case render_format_t::ASTC_10X10_UNORM:
        case render_format_t::ASTC_10X10_SRGB:
        case render_format_t::ASTC_12X10_UNORM:
        case render_format_t::ASTC_12X10_SRGB:
        case render_format_t::ASTC_12X12_UNORM:
        case render_format_t::ASTC_12X12_SRGB: {
            const u32 i = static_cast<u32>( storageFormat ) -
                          static_cast<u32>( render_format_t::ASTC_4X4_UNORM );
            static constexpr u8 widths[]{
                4u, 4u, 5u, 5u, 5u, 5u, 6u, 6u, 6u, 6u, 8u, 8u,
                8u, 8u, 8u, 8u, 10u, 10u, 10u, 10u, 10u, 10u, 10u,
                10u, 12u, 12u, 12u, 12u
            };
            static constexpr u8 heights[]{
                4u, 4u, 4u, 4u, 5u, 5u, 5u, 5u, 6u, 6u, 5u, 5u,
                6u, 6u, 8u, 8u, 5u, 5u, 6u, 6u, 8u, 8u, 10u,
                10u, 10u, 10u, 12u, 12u
            };
            if ( i >= sizeof( widths ) ) return CY_FALSE;
            layout = { widths[i], heights[i], 1u, 16u, CY_TRUE,
                       static_cast<bool_t>( ( i & 1u ) != 0u ), CY_TRUE };
            break;
        }
        case render_format_t::UNKNOWN:
        case render_format_t::INVALID:
        default:
            return CY_FALSE;
    }
    *pLayoutOut = layout;
    return CY_TRUE;
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
    if ( nHeight > nLargest ) nLargest = nHeight;
    if ( nDepth > nLargest ) nLargest = nDepth;
    u32 nMipLevels = 1u;
    while ( nLargest > 1u ) {
        nLargest >>= 1u;
        ++nMipLevels;
    }
    return nMipLevels;
}

u32 CookedTexture_SubresourceCount(
    const cooked_texture_desc_t &texture ) noexcept
{
    u64 count = texture.nMipLevels;
    if ( !CheckedMultiplyU64( count, texture.nFrames, count ) ||
         !CheckedMultiplyU64( count, texture.nLayers, count ) ||
         !CheckedMultiplyU64( count, texture.nFaces, count ) ||
         count > CY_U32_MAX ) {
        return 0u;
    }
    return static_cast<u32>( count );
}

usize CookedTexture_MetadataSize( u32 nSubresources ) noexcept
{
    if ( nSubresources == 0u ||
         nSubresources > CY_COOKED_TEXTURE_MAX_SUBRESOURCES ) {
        return 0u;
    }
    return CY_COOKED_TEXTURE_METADATA_HEADER_SIZE +
           static_cast<usize>( nSubresources ) *
               CY_COOKED_TEXTURE_SUBRESOURCE_RECORD_SIZE;
}

usize CookedTexture_RequiredSizeSubresources(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_subresource_source_t> subresources ) noexcept
{
    if ( !Span_IsValid( subresources ) ) {
        return 0u;
    }
    cooked_texture_desc_t normalized{};
    if ( NormalizeTexture( texture, CY_TRUE, normalized ) !=
             cooked_texture_status_t::OK ||
         subresources.nCount != normalized.nSubresources ) {
        return 0u;
    }
    for ( usize i = 0u; i < subresources.nCount; ++i ) {
        // Nested payload views must be validated before canonical layout
        // preparation hashes them. A non-null span does not imply that each
        // binary block inside it is valid.
        if ( !BinaryBlock_IsValid( subresources.pData[i].pixels ) ) {
            return 0u;
        }
    }
    usize cbFile = 0u;
    if ( !PrepareCanonicalLayout(
             normalized,
             subresources,
             cbFile,
             nullptr ) ) {
        return 0u;
    }
    return cbFile;
}

usize CookedTexture_RequiredSize(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_mip_source_t> mips ) noexcept
{
    if ( !Span_IsValid( mips ) ||
         mips.nCount > CY_COOKED_TEXTURE_MAX_MIP_LEVELS ) {
        return 0u;
    }
    cooked_texture_desc_t normalized{};
    if ( NormalizeTexture( texture, CY_TRUE, normalized ) !=
             cooked_texture_status_t::OK ||
         normalized.dimension != render_texture_dimension_t::TEXTURE_2D ||
         normalized.nFrames != 1u || normalized.nLayers != 1u ||
         normalized.nFaces != 1u || mips.nCount != normalized.nMipLevels ) {
        return 0u;
    }
    cooked_texture_subresource_source_t
        sources[CY_COOKED_TEXTURE_MAX_MIP_LEVELS]{};
    for ( usize i = 0u; i < mips.nCount; ++i ) {
        u32 nWidth = 0u;
        u32 nHeight = 0u;
        u32 nDepth = 0u;
        u32 cbRow = 0u;
        u32 cbSlice = 0u;
        u64 cbData = 0u;
        if ( !ExpectedSubresourceLayout(
                 normalized,
                 static_cast<u32>( i ),
                 nWidth,
                 nHeight,
                 nDepth,
                 cbRow,
                 cbSlice,
                 cbData ) ) {
            return 0u;
        }
        sources[i] = {
            static_cast<u32>( i ), 0u, 0u, 0u,
            mips.pData[i].nWidth,
            mips.pData[i].nHeight,
            mips.pData[i].nDepth,
            mips.pData[i].cbRowPitch,
            cbSlice,
            mips.pData[i].pixels
        };
    }
    return CookedTexture_RequiredSizeSubresources(
        normalized,
        { sources, mips.nCount } );
}

cooked_texture_result_t CookedTexture_WriteMetadata(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_mip_desc_t> mips,
    byte_span_t output ) noexcept
{
    cooked_texture_result_t result{};
    if ( !Span_IsValid( mips ) || !Span_IsValid( output ) ||
         mips.nCount > CY_COOKED_TEXTURE_MAX_MIP_LEVELS ) {
        result.status = cooked_texture_status_t::INVALID_ARGUMENT;
        return result;
    }
    result.cbRequired = CookedTexture_MetadataSize(
        static_cast<u32>( mips.nCount ) );
    if ( result.cbRequired != 0u && output.nCount >= result.cbRequired &&
         ( Cy_MemRangesOverlap(
               output.pData,
               result.cbRequired,
               &texture,
               sizeof( texture ) ) ||
           Cy_MemRangesOverlap(
               output.pData,
               result.cbRequired,
               mips.pData,
               mips.nCount * sizeof( cooked_texture_mip_desc_t ) ) ) ) {
        result.status = cooked_texture_status_t::INVALID_ARGUMENT;
        return result;
    }
    cooked_texture_desc_t normalized{};
    result.status = NormalizeTexture( texture, CY_TRUE, normalized );
    if ( result.status != cooked_texture_status_t::OK ||
         normalized.dimension != render_texture_dimension_t::TEXTURE_2D ||
         normalized.nFrames != 1u || normalized.nLayers != 1u ||
         normalized.nFaces != 1u || mips.nCount != normalized.nMipLevels ) {
        if ( result.status == cooked_texture_status_t::OK ) {
            result.status = cooked_texture_status_t::INVALID_MIP_CHAIN;
        }
        return result;
    }
    cooked_texture_subresource_desc_t
        entries[CY_COOKED_TEXTURE_MAX_MIP_LEVELS]{};
    for ( usize i = 0u; i < mips.nCount; ++i ) {
        const cooked_texture_mip_desc_t &mip = mips.pData[i];
        u32 cbSlice = 0u;
        u32 expectedWidth = 0u;
        u32 expectedHeight = 0u;
        u32 expectedDepth = 0u;
        u32 expectedRow = 0u;
        u64 expectedData = 0u;
        if ( !ExpectedSubresourceLayout(
                 normalized,
                 static_cast<u32>( i ),
                 expectedWidth,
                 expectedHeight,
                 expectedDepth,
                 expectedRow,
                 cbSlice,
                 expectedData ) ) {
            result.status = cooked_texture_status_t::INVALID_MIP_CHAIN;
            result.iMip = i;
            return result;
        }
        entries[i] = {
            mip.nLevel, 0u, 0u, 0u, mip.nWidth, mip.nHeight, mip.nDepth,
            mip.cbRowPitch, cbSlice, mip.iDataChunk, 0u, 0u, mip.cbData, 0u
        };
    }
    result.status = ValidateSubresources(
        normalized,
        { entries, mips.nCount },
        &result.iSubresource );
    if ( result.status != cooked_texture_status_t::OK ) {
        result.iMip = result.iSubresource;
        return result;
    }
    return WriteMetadataV2(
        normalized,
        { entries, mips.nCount },
        output );
}

cooked_texture_result_t CookedTexture_WriteSubresources(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_subresource_source_t> subresources,
    content_hash_t sourceHash,
    byte_span_t output ) noexcept
{
    cooked_texture_result_t result{};
    if ( !Span_IsValid( subresources ) || !Span_IsValid( output ) ) {
        result.status = cooked_texture_status_t::INVALID_ARGUMENT;
        return result;
    }
    cooked_texture_desc_t normalized{};
    result.status = NormalizeTexture( texture, CY_TRUE, normalized );
    if ( result.status != cooked_texture_status_t::OK ) {
        return result;
    }
    if ( subresources.nCount != normalized.nSubresources ) {
        result.status = cooked_texture_status_t::INVALID_SUBRESOURCE_LAYOUT;
        return result;
    }
    for ( usize i = 0u; i < subresources.nCount; ++i ) {
        if ( !BinaryBlock_IsValid( subresources.pData[i].pixels ) ) {
            result.status = cooked_texture_status_t::INVALID_DATA;
            result.iSubresource = i;
            result.iMip = subresources.pData[i].nMipLevel;
            return result;
        }
    }

    if ( !PrepareCanonicalLayout(
             normalized,
             subresources,
             result.cbRequired,
             &result.iSubresource ) ) {
        result.status = cooked_texture_status_t::INVALID_SUBRESOURCE_LAYOUT;
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
             subresources.pData,
             subresources.nCount * sizeof( cooked_texture_subresource_source_t ) ) ) {
        result.status = cooked_texture_status_t::INVALID_ARGUMENT;
        return result;
    }
    for ( usize i = 0u; i < subresources.nCount; ++i ) {
        if ( Cy_MemRangesOverlap(
                 output.pData,
                 result.cbRequired,
                 subresources.pData[i].pixels.pData,
                 subresources.pData[i].pixels.cbSize ) ) {
            result.status = cooked_texture_status_t::INVALID_ARGUMENT;
            result.iSubresource = i;
            return result;
        }
    }

    const u32 nChunks = static_cast<u32>( subresources.nCount + 1u );
    const usize cbPrefix = CookedResource_PrefixSize( nChunks );
    usize iMetadata = cbPrefix;
    if ( !Cy_AlignUpChecked(
             iMetadata,
             CY_COOKED_TEXTURE_METADATA_ALIGNMENT,
             iMetadata ) ) {
        result.status = cooked_texture_status_t::RESOURCE_ERROR;
        result.resourceStatus = cooked_resource_status_t::INVALID_CHUNK;
        return result;
    }
    if ( iMetadata > cbPrefix ) {
        Cy_MemZero(
            output.pData + cbPrefix,
            iMetadata - cbPrefix );
    }

    cooked_resource_header_t header{};
    header.resourceType = CY_RENDER_TEXTURE_RESOURCE_TYPE;
    header.nResourceVersion = CY_COOKED_TEXTURE_RESOURCE_VERSION_CURRENT;
    header.nChunks = nChunks;
    header.cbFile = result.cbRequired;
    if ( ContentHash_IsValid( sourceHash ) ) {
        header.flags |= COOKED_RESOURCE_FLAG_HAS_SOURCE_HASH;
        header.sourceHash = sourceHash;
    }

    const usize cbMetadata = CookedTexture_MetadataSize(
        static_cast<u32>( subresources.nCount ) );
    byte_span_t metadataOutput{
        output.pData + iMetadata,
        cbMetadata
    };
    if ( !WriteMetadataV2FromSources(
        normalized,
        subresources,
        metadataOutput ) ) {
        result.status = cooked_texture_status_t::RESOURCE_ERROR;
        result.resourceStatus = cooked_resource_status_t::INVALID_CHUNK;
        return result;
    }
    cooked_chunk_desc_t metadataChunk{};
    metadataChunk.chunkType = CY_COOKED_TEXTURE_METADATA_CHUNK;
    metadataChunk.nAlignment = CY_COOKED_TEXTURE_METADATA_ALIGNMENT;
    metadataChunk.iOffset = iMetadata;
    metadataChunk.cbStored = cbMetadata;
    metadataChunk.cbDecoded = cbMetadata;
    metadataChunk.flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    metadataChunk.contentHash = ContentHash_Data( {
        metadataOutput.pData,
        metadataOutput.nCount
    } );
    if ( !ContentHash_IsValid( metadataChunk.contentHash ) ) {
        result.status = cooked_texture_status_t::RESOURCE_ERROR;
        result.resourceStatus = cooked_resource_status_t::INVALID_CHUNK;
        result.iChunk = 0u;
        return result;
    }

    usize iPayloadEnd = iMetadata + cbMetadata;
    for ( usize i = 0u; i < subresources.nCount; ++i ) {
        usize iPayload = iPayloadEnd;
        if ( !Cy_AlignUpChecked(
                 iPayload,
                 CY_COOKED_TEXTURE_DATA_ALIGNMENT,
                 iPayload ) ) {
            result.status = cooked_texture_status_t::RESOURCE_ERROR;
            result.resourceStatus = cooked_resource_status_t::INVALID_CHUNK;
            result.iChunk = i + 1u;
            return result;
        }
        if ( iPayload > iPayloadEnd ) {
            Cy_MemZero(
                output.pData + iPayloadEnd,
                iPayload - iPayloadEnd );
        }
        Cy_MemCopy(
            output.pData + iPayload,
            subresources.pData[i].pixels.pData,
            subresources.pData[i].pixels.cbSize );
        iPayloadEnd = iPayload + subresources.pData[i].pixels.cbSize;
    }

    if ( iPayloadEnd != result.cbRequired ||
         !WriteTextureChunkTable(
             header,
             metadataChunk,
             subresources,
             output ) ) {
        result.status = cooked_texture_status_t::RESOURCE_ERROR;
        result.resourceStatus = cooked_resource_status_t::INVALID_CHUNK;
        return result;
    }
    header.flags |= COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH;
    header.contentHash = CookedResource_ComputeContentHash( {
        output.pData,
        result.cbRequired
    } );
    if ( !ContentHash_IsValid( header.contentHash ) ||
         !RewriteResourceHeader( header, output ) ) {
        result.status = cooked_texture_status_t::RESOURCE_ERROR;
        result.resourceStatus = cooked_resource_status_t::INVALID_HEADER;
        return result;
    }
    result.cbWritten = result.cbRequired;
    return result;
}

cooked_texture_result_t CookedTexture_Write(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_mip_source_t> mips,
    content_hash_t sourceHash,
    byte_span_t output ) noexcept
{
    cooked_texture_result_t result{};
    if ( !Span_IsValid( mips ) || !Span_IsValid( output ) ||
         mips.nCount > CY_COOKED_TEXTURE_MAX_MIP_LEVELS ) {
        result.status = cooked_texture_status_t::INVALID_ARGUMENT;
        return result;
    }
    const usize cbRequired = CookedTexture_RequiredSize( texture, mips );
    if ( cbRequired != 0u && output.nCount >= cbRequired &&
         ( Cy_MemRangesOverlap(
               output.pData,
               cbRequired,
               &texture,
               sizeof( texture ) ) ||
           Cy_MemRangesOverlap(
               output.pData,
               cbRequired,
               mips.pData,
               mips.nCount * sizeof( cooked_texture_mip_source_t ) ) ) ) {
        result.status = cooked_texture_status_t::INVALID_ARGUMENT;
        return result;
    }
    cooked_texture_desc_t normalized{};
    result.status = NormalizeTexture( texture, CY_TRUE, normalized );
    if ( result.status != cooked_texture_status_t::OK ) return result;
    if ( normalized.dimension != render_texture_dimension_t::TEXTURE_2D ||
         normalized.nFrames != 1u || normalized.nLayers != 1u ||
         normalized.nFaces != 1u || mips.nCount != normalized.nMipLevels ) {
        result.status = cooked_texture_status_t::INVALID_MIP_CHAIN;
        return result;
    }
    cooked_texture_subresource_source_t
        sources[CY_COOKED_TEXTURE_MAX_MIP_LEVELS]{};
    for ( usize i = 0u; i < mips.nCount; ++i ) {
        u32 nWidth = 0u;
        u32 nHeight = 0u;
        u32 nDepth = 0u;
        u32 cbRow = 0u;
        u32 cbSlice = 0u;
        u64 cbData = 0u;
        if ( !ExpectedSubresourceLayout(
                 normalized,
                 static_cast<u32>( i ),
                 nWidth,
                 nHeight,
                 nDepth,
                 cbRow,
                 cbSlice,
                 cbData ) ) {
            result.status = cooked_texture_status_t::INVALID_MIP_CHAIN;
            result.iMip = i;
            return result;
        }
        sources[i] = {
            static_cast<u32>( i ), 0u, 0u, 0u,
            mips.pData[i].nWidth,
            mips.pData[i].nHeight,
            mips.pData[i].nDepth,
            mips.pData[i].cbRowPitch,
            cbSlice,
            mips.pData[i].pixels
        };
    }
    return CookedTexture_WriteSubresources(
        normalized,
        { sources, mips.nCount },
        sourceHash,
        output );
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
    const cooked_resource_result_t layout = ReadResourceLayoutStreaming(
        input,
        header );
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
    if ( header.nResourceVersion != CY_COOKED_TEXTURE_RESOURCE_VERSION_V1 &&
         header.nResourceVersion != CY_COOKED_TEXTURE_RESOURCE_VERSION_V2 ) {
        result.status = cooked_texture_status_t::VERSION_MISMATCH;
        return result;
    }
    if ( ( header.flags & COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH ) == 0u ) {
        result.status = cooked_texture_status_t::INVALID_FLAGS;
        return result;
    }
    const u32 nMaximumChunks = header.nResourceVersion ==
            CY_COOKED_TEXTURE_RESOURCE_VERSION_V1
        ? CY_COOKED_TEXTURE_MAX_MIP_LEVELS + 1u
        : CY_COOKED_TEXTURE_MAX_SUBRESOURCES + 1u;
    if ( header.nChunks < 2u || header.nChunks > nMaximumChunks ) {
        result.status = cooked_texture_status_t::INVALID_CHUNK_COUNT;
        return result;
    }

    cooked_chunk_desc_t metadataChunk{};
    if ( !ReadChunkAt( input, 0u, metadataChunk ) ) {
        result.status = cooked_texture_status_t::RESOURCE_ERROR;
        result.resourceStatus = cooked_resource_status_t::TRUNCATED_INPUT;
        result.iChunk = 0u;
        return result;
    }
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
             data_byte_order_t::LITTLE ) ) {
        result.status = cooked_texture_status_t::INVALID_METADATA;
        return result;
    }
    const bool_t bVersion1 = header.nResourceVersion ==
        CY_COOKED_TEXTURE_RESOURCE_VERSION_V1;
    if ( ( bVersion1 && !ReadMetadataHeaderV1( reader, metadata ) ) ||
         ( !bVersion1 && !ReadMetadataHeaderV2( reader, metadata ) ) ) {
        result.status = cooked_texture_status_t::INVALID_METADATA;
        return result;
    }

    cooked_texture_desc_t normalized{};
    result.status = NormalizeTexture( metadata.texture, CY_FALSE, normalized );
    if ( result.status != cooked_texture_status_t::OK ) return result;
    if ( normalized.nSubresources + 1u != header.nChunks ) {
        result.status = cooked_texture_status_t::INVALID_CHUNK_COUNT;
        return result;
    }
    const usize cbExpectedMetadata = bVersion1
        ? CY_COOKED_TEXTURE_METADATA_HEADER_SIZE_V1 +
            static_cast<usize>( normalized.nMipLevels ) *
                CY_COOKED_TEXTURE_MIP_RECORD_SIZE_V1
        : CookedTexture_MetadataSize( normalized.nSubresources );
    if ( metadataBytes.cbSize != cbExpectedMetadata ) {
        result.status = cooked_texture_status_t::INVALID_CHUNK_COUNT;
        return result;
    }

    const u64 cbSerializedData = metadata.texture.cbData;
    const u64 cbSerializedResident = metadata.texture.cbResidentData;
    u64 cbTotal = 0u;
    u64 cbResident = 0u;
    for ( usize i = 0u; i < normalized.nSubresources; ++i ) {
        cooked_texture_subresource_desc_t entry{};
        result.status = ReadSubresourceEntry(
            reader,
            bVersion1,
            normalized,
            i,
            entry );
        if ( result.status != cooked_texture_status_t::OK ) {
            if ( bVersion1 ) {
                result.iMip = i;
            } else {
                result.iSubresource = i;
            }
            return result;
        }
        result.status = ValidateSubresource(
            normalized,
            entry,
            i,
            cbTotal,
            cbResident );
        if ( result.status != cooked_texture_status_t::OK ) {
            result.iSubresource = i;
            result.iMip = entry.nMipLevel;
            return result;
        }
    }
    normalized.cbData = cbTotal;
    normalized.cbResidentData = cbResident;
    if ( !bVersion1 &&
         ( normalized.cbData != cbSerializedData ||
           normalized.cbResidentData != cbSerializedResident ) ) {
        result.status = cooked_texture_status_t::INVALID_SUBRESOURCE_LAYOUT;
        return result;
    }

    cooked_texture_view_t texture{};
    texture.desc = normalized;
    texture.sourceHash = header.sourceHash;
    texture.nMipLevels = normalized.nMipLevels;
    texture.nSubresources = normalized.nSubresources;
    texture.nResourceVersion = header.nResourceVersion;
    texture.fileBytes = input;
    texture.metadataBytes = metadataBytes;

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

    const usize cbMetadataHeader = bVersion1
        ? CY_COOKED_TEXTURE_METADATA_HEADER_SIZE_V1
        : CY_COOKED_TEXTURE_METADATA_HEADER_SIZE;
    if ( !ByteReader_Seek( &reader, cbMetadataHeader ) ) {
        result.status = cooked_texture_status_t::INVALID_METADATA;
        return result;
    }
    for ( usize i = 0u; i < normalized.nSubresources; ++i ) {
        cooked_texture_subresource_desc_t entry{};
        result.status = ReadSubresourceEntry(
            reader,
            bVersion1,
            normalized,
            i,
            entry );
        if ( result.status != cooked_texture_status_t::OK ) {
            if ( bVersion1 ) {
                result.iMip = i;
            } else {
                result.iSubresource = i;
            }
            return result;
        }

        cooked_chunk_desc_t chunk{};
        if ( !ReadChunkAt( input, entry.iDataChunk, chunk ) ) {
            result.status = cooked_texture_status_t::INVALID_DATA_CHUNK;
            result.iSubresource = i;
            result.iMip = entry.nMipLevel;
            result.iChunk = entry.iDataChunk;
            return result;
        }
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
            result.iSubresource = i;
            result.iMip = entry.nMipLevel;
            result.iChunk = entry.iDataChunk;
            return result;
        }
        if ( chunk.chunkType != CY_COOKED_TEXTURE_DATA_CHUNK ||
             chunk.codec != cooked_chunk_codec_t::NONE ||
             chunk.flags != COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ||
             chunk.nAlignment != CY_COOKED_TEXTURE_DATA_ALIGNMENT ||
             chunk.cbStored != entry.cbData ||
             chunk.cbDecoded != entry.cbData ) {
            result.status = cooked_texture_status_t::INVALID_DATA_CHUNK;
            result.iSubresource = i;
            result.iMip = entry.nMipLevel;
            result.iChunk = entry.iDataChunk;
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
            result.iSubresource = i;
            result.iMip = entry.nMipLevel;
            result.iChunk = entry.iDataChunk;
            return result;
        }
        if ( entry.nFrame == 0u && entry.nLayer == 0u &&
             entry.nFace == 0u ) {
            texture.mips[entry.nMipLevel] = {
                entry.nMipLevel,
                entry.nWidth,
                entry.nHeight,
                entry.nDepth,
                entry.cbRowPitch,
                pixels,
                chunk.contentHash
            };
        }
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
    for ( usize i = 0u; i < texture.nMipLevels; ++i ) {
        if ( texture.mips[i].nLevel == nLevel ) {
            return &texture.mips[i];
        }
    }
    return nullptr;
}

bool_t CookedTexture_GetSubresource(
    const cooked_texture_view_t &texture,
    u32 nMipLevel,
    u32 nFrame,
    u32 nLayer,
    u32 nFace,
    cooked_texture_subresource_view_t *pSubresourceOut ) noexcept
{
    if ( pSubresourceOut == nullptr ||
         nMipLevel >= texture.desc.nMipLevels ||
         nFrame >= texture.desc.nFrames ||
         nLayer >= texture.desc.nLayers ||
         nFace >= texture.desc.nFaces ||
         Cy_MemRangesOverlap(
             pSubresourceOut,
             sizeof( *pSubresourceOut ),
             &texture,
             sizeof( texture ) ) ||
         Cy_MemRangesOverlap(
             pSubresourceOut,
             sizeof( *pSubresourceOut ),
             texture.fileBytes.pData,
             texture.fileBytes.cbSize ) ||
         Cy_MemRangesOverlap(
             pSubresourceOut,
             sizeof( *pSubresourceOut ),
             texture.metadataBytes.pData,
             texture.metadataBytes.cbSize ) ) {
        return CY_FALSE;
    }
    if ( texture.nResourceVersion == CY_COOKED_TEXTURE_RESOURCE_VERSION_V1 ) {
        if ( nFrame != 0u || nLayer != 0u || nFace != 0u ) return CY_FALSE;
        const cooked_texture_mip_view_t *pMip = CookedTexture_FindMip(
            texture,
            nMipLevel );
        if ( pMip == nullptr ) return CY_FALSE;
        cooked_texture_subresource_view_t view{};
        view.nMipLevel = pMip->nLevel;
        view.nWidth = pMip->nWidth;
        view.nHeight = pMip->nHeight;
        view.nDepth = pMip->nDepth;
        view.cbRowPitch = pMip->cbRowPitch;
        view.cbSlicePitch = pMip->cbRowPitch * pMip->nHeight;
        view.pixels = pMip->pixels;
        view.contentHash = pMip->contentHash;
        *pSubresourceOut = view;
        return CY_TRUE;
    }
    if ( texture.nResourceVersion != CY_COOKED_TEXTURE_RESOURCE_VERSION_V2 ||
         !BinaryBlock_IsValid( texture.fileBytes ) ||
         !BinaryBlock_IsValid( texture.metadataBytes ) ) {
        return CY_FALSE;
    }

    const u32 iSubresource = ( ( ( nMipLevel * texture.desc.nFrames + nFrame ) *
                                  texture.desc.nLayers + nLayer ) *
                                texture.desc.nFaces ) + nFace;
    const usize iRecord = CY_COOKED_TEXTURE_METADATA_HEADER_SIZE +
        static_cast<usize>( iSubresource ) *
            CY_COOKED_TEXTURE_SUBRESOURCE_RECORD_SIZE;
    if ( iRecord > texture.metadataBytes.cbSize ||
         CY_COOKED_TEXTURE_SUBRESOURCE_RECORD_SIZE >
             texture.metadataBytes.cbSize - iRecord ) {
        return CY_FALSE;
    }
    byte_reader_t reader{};
    cooked_texture_subresource_desc_t entry{};
    if ( !ByteReader_Init(
             &reader,
             { texture.metadataBytes.pData + iRecord,
               CY_COOKED_TEXTURE_SUBRESOURCE_RECORD_SIZE },
             data_byte_order_t::LITTLE ) ||
         !ReadSubresourceDescriptor( reader, entry ) ) {
        return CY_FALSE;
    }
    cooked_chunk_desc_t chunk{};
    if ( !ReadChunkAt( texture.fileBytes, entry.iDataChunk, chunk ) ||
         entry.nMipLevel != nMipLevel || entry.nFrame != nFrame ||
         entry.nLayer != nLayer || entry.nFace != nFace ||
         entry.iDataChunk != iSubresource + 1u || entry.flags != 0u ||
         entry.nReserved != 0u || entry.nReserved64 != 0u ||
         chunk.chunkType != CY_COOKED_TEXTURE_DATA_CHUNK ||
         chunk.codec != cooked_chunk_codec_t::NONE ||
         chunk.flags != COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ||
         chunk.nAlignment != CY_COOKED_TEXTURE_DATA_ALIGNMENT ||
         chunk.cbStored != entry.cbData || chunk.cbDecoded != entry.cbData ||
         chunk.iOffset > texture.fileBytes.cbSize ||
         chunk.cbStored > texture.fileBytes.cbSize - chunk.iOffset ) {
        return CY_FALSE;
    }
    cooked_texture_subresource_view_t view{};
    view.nMipLevel = entry.nMipLevel;
    view.nFrame = entry.nFrame;
    view.nLayer = entry.nLayer;
    view.nFace = entry.nFace;
    view.nWidth = entry.nWidth;
    view.nHeight = entry.nHeight;
    view.nDepth = entry.nDepth;
    view.cbRowPitch = entry.cbRowPitch;
    view.cbSlicePitch = entry.cbSlicePitch;
    view.pixels = {
        texture.fileBytes.pData + chunk.iOffset,
        static_cast<usize>( chunk.cbStored )
    };
    view.contentHash = chunk.contentHash;
    *pSubresourceOut = view;
    return CY_TRUE;
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
        case cooked_texture_status_t::INVALID_ALPHA_MODE: return "INVALID_ALPHA_MODE";
        case cooked_texture_status_t::INVALID_TARGET: return "INVALID_TARGET";
        case cooked_texture_status_t::INVALID_RESIDENCY: return "INVALID_RESIDENCY";
        case cooked_texture_status_t::SUBRESOURCE_LIMIT_EXCEEDED: return "SUBRESOURCE_LIMIT_EXCEEDED";
        case cooked_texture_status_t::INVALID_SUBRESOURCE_LAYOUT: return "INVALID_SUBRESOURCE_LAYOUT";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
