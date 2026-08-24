//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageCodec.cpp
//  Purpose: Implements reusable image-file decoding for Cypher authoring tools.
//  Details: Each decoder validates dimensions and allocation limits before
//           publishing an owned surface. Decoded rows use the image subsystem's
//           top-left origin and native in-memory numeric representation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Image Codec Implementation Notes

Codec adapters translate external image files into validated engine image descriptions. Decoded
dimensions, channel layout, allocation size, and row pitch are checked before publishing a
surface.
================
*/

#include "CypherCommon_ImageCodec.h"

#include "CypherCommon_ImageView.h"
#include "CypherCommon_MemoryOps.h"
#include "CypherCommon_StringPath.h"

#include <png.h>
#include <tinyexr.h>
#include <turbojpeg.h>

#include <cmath>
#include <cstdlib>
#include <limits>

namespace cypher::common
{

namespace
{

template <usize nExtent>
constexpr string_view_t CodecText( const char ( &text )[nExtent] ) noexcept
{
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

struct turbojpeg_owner_t {
    tjhandle handle{ nullptr }; // Opaque libjpeg-turbo decompressor handle.

    turbojpeg_owner_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( turbojpeg_owner_t );

    ~turbojpeg_owner_t() noexcept
    {
        if ( handle != nullptr ) {
            tj3Destroy( handle );
        }
    }
};

struct exr_pixels_owner_t {
    float *pPixels{ nullptr }; // Temporary RGBA array allocated by TinyEXR.

    exr_pixels_owner_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( exr_pixels_owner_t );

    ~exr_pixels_owner_t() noexcept
    {
        std::free( pPixels );
    }
};

bool_t ImageCodec_IsFormatValid( image_file_format_t format ) noexcept
{
    return format == image_file_format_t::UNKNOWN ||
           format == image_file_format_t::PNG ||
           format == image_file_format_t::JPEG ||
           format == image_file_format_t::TGA ||
           format == image_file_format_t::EXR;
}

bool_t ImageCodec_AreOptionsValid(
    const image_decode_options_t &options ) noexcept
{
    return ImageCodec_IsFormatValid( options.formatHint ) &&
           options.nMaximumDimension > 0u &&
           options.cbMaximumDecodedSize > 0u &&
           options.cbRowAlignment > 0u &&
           ( options.cbRowAlignment & ( options.cbRowAlignment - 1u ) ) == 0u;
}

image_codec_status_t ImageCodec_CreateSurface(
    image_surface_t &surface,
    const allocator_t *pAllocator,
    const image_desc_t &desc,
    const image_decode_options_t &options ) noexcept
{
    if ( desc.extent.nWidth == 0u || desc.extent.nHeight == 0u ||
         desc.extent.nDepth != 1u ||
         desc.extent.nWidth > options.nMaximumDimension ||
         desc.extent.nHeight > options.nMaximumDimension ) {
        return image_codec_status_t::INVALID_DIMENSIONS;
    }

    // Calculate and limit the complete padded allocation before asking the
    // allocator for memory. Codec dimensions are untrusted file input.
    const image_layout_result_t layout = ImageFormat_CalculateLayout(
        desc,
        options.cbRowAlignment );
    if ( layout.status != image_format_status_t::OK ) {
        return layout.status == image_format_status_t::ARITHMETIC_OVERFLOW
            ? image_codec_status_t::SIZE_LIMIT_EXCEEDED
            : image_codec_status_t::INVALID_DIMENSIONS;
    }
    if ( layout.layout.cbTotalSize > options.cbMaximumDecodedSize ) {
        return image_codec_status_t::SIZE_LIMIT_EXCEEDED;
    }

    const image_surface_status_t status = ImageSurface_Create(
        &surface,
        pAllocator,
        desc,
        image_surface_init_t::UNINITIALIZED,
        options.cbRowAlignment );
    return status == image_surface_status_t::OK
        ? image_codec_status_t::OK
        : image_codec_status_t::ALLOCATION_FAILED;
}

image_codec_status_t ImageCodec_DecodePng(
    binary_block_t source,
    const allocator_t *pAllocator,
    const image_decode_options_t &options,
    image_surface_t &surface ) noexcept
{
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    if ( !png_image_begin_read_from_memory(
             &png,
             source.pData,
             source.cbSize ) ) {
        png_image_free( &png );
        return image_codec_status_t::MALFORMED_DATA;
    }

    // libpng's simplified API normalizes palette, grayscale, and source alpha
    // variants into the one authoring representation used by this boundary.
    png.format = PNG_FORMAT_RGBA;
    const image_desc_t desc{
        { png.width, png.height, 1u },
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT
    };
    const image_codec_status_t createStatus = ImageCodec_CreateSurface(
        surface,
        pAllocator,
        desc,
        options );
    if ( createStatus != image_codec_status_t::OK ) {
        png_image_free( &png );
        return createStatus;
    }

    const image_view_t view = ImageSurface_GetView( &surface );
    if ( view.cbRowPitch >
         static_cast<usize>( std::numeric_limits<png_int_32>::max() ) ||
         !png_image_finish_read(
             &png,
             nullptr,
             view.pixels.pData,
             static_cast<png_int_32>( view.cbRowPitch ),
             nullptr ) ) {
        png_image_free( &png );
        return image_codec_status_t::MALFORMED_DATA;
    }

    png_image_free( &png );
    return image_codec_status_t::OK;
}

image_codec_status_t ImageCodec_DecodeJpeg(
    binary_block_t source,
    const allocator_t *pAllocator,
    const image_decode_options_t &options,
    image_surface_t &surface ) noexcept
{
    turbojpeg_owner_t decoder{};
    decoder.handle = tj3Init( TJINIT_DECOMPRESS );
    if ( decoder.handle == nullptr ||
         tj3DecompressHeader(
             decoder.handle,
             source.pData,
             source.cbSize ) != 0 ) {
        return image_codec_status_t::MALFORMED_DATA;
    }

    const int nWidth = tj3Get( decoder.handle, TJPARAM_JPEGWIDTH );
    const int nHeight = tj3Get( decoder.handle, TJPARAM_JPEGHEIGHT );
    const int nPrecision = tj3Get( decoder.handle, TJPARAM_PRECISION );
    // The current surface contract accepts common 8-bit JPEG data only. Higher
    // precision variants require a deliberate output-format policy.
    if ( nPrecision != 8 ) {
        return image_codec_status_t::UNSUPPORTED_ENCODING;
    }
    if ( nWidth <= 0 || nHeight <= 0 ) {
        return image_codec_status_t::INVALID_DIMENSIONS;
    }

    const image_desc_t desc{
        {
            static_cast<u32>( nWidth ),
            static_cast<u32>( nHeight ),
            1u
        },
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::NONE
    };
    const image_codec_status_t createStatus = ImageCodec_CreateSurface(
        surface,
        pAllocator,
        desc,
        options );
    if ( createStatus != image_codec_status_t::OK ) {
        return createStatus;
    }

    const image_view_t view = ImageSurface_GetView( &surface );
    if ( view.cbRowPitch >
         static_cast<usize>( std::numeric_limits<int>::max() ) ) {
        return image_codec_status_t::SIZE_LIMIT_EXCEEDED;
    }
    return tj3Decompress8(
               decoder.handle,
               source.pData,
               source.cbSize,
               view.pixels.pData,
               static_cast<int>( view.cbRowPitch ),
               TJPF_RGBA ) == 0
        ? image_codec_status_t::OK
        : image_codec_status_t::MALFORMED_DATA;
}

image_codec_status_t ImageCodec_DecodeExr(
    binary_block_t source,
    const allocator_t *pAllocator,
    const image_decode_options_t &options,
    image_surface_t &surface ) noexcept
{
    exr_pixels_owner_t decoded{};
    int nWidth = 0;
    int nHeight = 0;
    const char *pError = nullptr;
    const int exrStatus = LoadEXRFromMemory(
        &decoded.pPixels,
        &nWidth,
        &nHeight,
        source.pData,
        source.cbSize,
        &pError );
    if ( exrStatus != TINYEXR_SUCCESS ) {
        if ( pError != nullptr ) {
            FreeEXRErrorMessage( pError );
        }
        return image_codec_status_t::MALFORMED_DATA;
    }
    if ( pError != nullptr ) {
        FreeEXRErrorMessage( pError );
    }
    if ( nWidth <= 0 || nHeight <= 0 ) {
        return image_codec_status_t::INVALID_DIMENSIONS;
    }

    // TinyEXR returns host floats in RGBA order. Validate every component before
    // publishing it because non-finite values destabilize later image filters.
    const u64 nComponentCount =
        static_cast<u64>( nWidth ) * static_cast<u64>( nHeight ) * 4u;
    if ( nComponentCount > CY_USIZE_MAX ) {
        return image_codec_status_t::SIZE_LIMIT_EXCEEDED;
    }
    for ( usize iComponent = 0u;
          iComponent < static_cast<usize>( nComponentCount );
          ++iComponent ) {
        if ( !std::isfinite( decoded.pPixels[iComponent] ) ) {
            return image_codec_status_t::NON_FINITE_COMPONENT;
        }
    }

    const image_desc_t desc{
        {
            static_cast<u32>( nWidth ),
            static_cast<u32>( nHeight ),
            1u
        },
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT
    };
    const image_codec_status_t createStatus = ImageCodec_CreateSurface(
        surface,
        pAllocator,
        desc,
        options );
    if ( createStatus != image_codec_status_t::OK ) {
        return createStatus;
    }

    const image_view_t view = ImageSurface_GetView( &surface );
    const usize cbSourceRow = static_cast<usize>( nWidth ) * 4u * sizeof( f32 );
    // Copy row by row so destination alignment padding remains untouched and
    // never becomes part of the logical image.
    for ( u32 iRow = 0u; iRow < desc.extent.nHeight; ++iRow ) {
        const byte_span_t row = ImageView_GetRow( view, iRow, 0u );
        Cy_MemCopy(
            row.pData,
            decoded.pPixels + static_cast<usize>( iRow ) * nWidth * 4u,
            cbSourceRow );
    }
    return image_codec_status_t::OK;
}

u16 ImageCodec_ReadLittleU16( const byte *pData ) noexcept
{
    return static_cast<u16>( pData[0] ) |
           static_cast<u16>( static_cast<u16>( pData[1] ) << 8u );
}

bool_t ImageCodec_ReadTgaPixel(
    binary_block_t source,
    usize &iCursor,
    u8 nPixelDepth,
    bool_t bGrayscale,
    byte rgba[4] ) noexcept
{
    const usize cbPixel = bGrayscale ? 1u : nPixelDepth / 8u;
    if ( iCursor > source.cbSize || cbPixel > source.cbSize - iCursor ) {
        return CY_FALSE;
    }

    // TGA stores true-color samples as BGR(A); normalize all variants to RGBA.
    if ( bGrayscale ) {
        rgba[0] = source.pData[iCursor];
        rgba[1] = source.pData[iCursor];
        rgba[2] = source.pData[iCursor];
        rgba[3] = 255u;
    } else {
        rgba[0] = source.pData[iCursor + 2u];
        rgba[1] = source.pData[iCursor + 1u];
        rgba[2] = source.pData[iCursor];
        rgba[3] = cbPixel == 4u ? source.pData[iCursor + 3u] : 255u;
    }
    iCursor += cbPixel;
    return CY_TRUE;
}

void ImageCodec_WriteTgaPixel(
    const image_view_t &view,
    u32 iFilePixel,
    bool_t bTopOrigin,
    bool_t bRightOrigin,
    const byte rgba[4] ) noexcept
{
    const u32 nWidth = view.desc.extent.nWidth;
    const u32 nHeight = view.desc.extent.nHeight;
    const u32 iFileColumn = iFilePixel % nWidth;
    const u32 iFileRow = iFilePixel / nWidth;
    // File-order coordinates are remapped into the image subsystem's fixed
    // top-left origin. No later consumer needs to remember TGA orientation bits.
    const u32 iColumn = bRightOrigin
        ? nWidth - 1u - iFileColumn
        : iFileColumn;
    const u32 iRow = bTopOrigin
        ? iFileRow
        : nHeight - 1u - iFileRow;
    const byte_span_t pixel = ImageView_GetPixel(
        view,
        iColumn,
        iRow,
        0u );
    Cy_MemCopy( pixel.pData, rgba, 4u );
}

image_codec_status_t ImageCodec_DecodeTga(
    binary_block_t source,
    const allocator_t *pAllocator,
    const image_decode_options_t &options,
    image_surface_t &surface ) noexcept
{
    constexpr usize TGA_HEADER_SIZE = 18u;
    if ( source.cbSize < TGA_HEADER_SIZE ) {
        return image_codec_status_t::MALFORMED_DATA;
    }

    const u8 cbImageId = source.pData[0];
    const u8 nColorMapType = source.pData[1];
    const u8 nImageType = source.pData[2];
    const u16 nWidth = ImageCodec_ReadLittleU16( source.pData + 12u );
    const u16 nHeight = ImageCodec_ReadLittleU16( source.pData + 14u );
    const u8 nPixelDepth = source.pData[16];
    const u8 descriptor = source.pData[17];
    const bool_t bGrayscale = nImageType == 3u || nImageType == 11u;
    const bool_t bRle = nImageType == 10u || nImageType == 11u;

    if ( nColorMapType != 0u ) {
        return image_codec_status_t::UNSUPPORTED_ENCODING;
    }
    if ( nImageType != 2u && nImageType != 3u &&
         nImageType != 10u && nImageType != 11u ) {
        return image_codec_status_t::UNSUPPORTED_ENCODING;
    }
    if ( ( bGrayscale && nPixelDepth != 8u ) ||
         ( !bGrayscale && nPixelDepth != 24u && nPixelDepth != 32u ) ) {
        return image_codec_status_t::UNSUPPORTED_ENCODING;
    }

    const image_desc_t desc{
        { nWidth, nHeight, 1u },
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        nPixelDepth == 32u
            ? image_alpha_mode_t::STRAIGHT
            : image_alpha_mode_t::NONE
    };
    const image_codec_status_t createStatus = ImageCodec_CreateSurface(
        surface,
        pAllocator,
        desc,
        options );
    if ( createStatus != image_codec_status_t::OK ) {
        return createStatus;
    }

    usize iCursor = TGA_HEADER_SIZE;
    if ( cbImageId > source.cbSize - iCursor ) {
        return image_codec_status_t::MALFORMED_DATA;
    }
    iCursor += cbImageId;

    const image_view_t view = ImageSurface_GetView( &surface );
    const bool_t bTopOrigin = ( descriptor & 0x20u ) != 0u;
    const bool_t bRightOrigin = ( descriptor & 0x10u ) != 0u;
    const u32 nPixelCount =
        static_cast<u32>( nWidth ) * static_cast<u32>( nHeight );
    u32 iPixel = 0u;

    // Both raw and RLE TGA variants are decoded into the same sequential file-
    // pixel stream, then orientation is applied by ImageCodec_WriteTgaPixel.
    while ( iPixel < nPixelCount ) {
        u32 nPacketPixels = 1u;
        bool_t bRunPacket = CY_FALSE;
        if ( bRle ) {
            if ( iCursor >= source.cbSize ) {
                return image_codec_status_t::MALFORMED_DATA;
            }
            const u8 packet = source.pData[iCursor++];
            nPacketPixels = static_cast<u32>( packet & 0x7Fu ) + 1u;
            bRunPacket = ( packet & 0x80u ) != 0u;
            if ( nPacketPixels > nPixelCount - iPixel ) {
                return image_codec_status_t::MALFORMED_DATA;
            }
        }

        byte rgba[4]{};
        if ( bRunPacket && !ImageCodec_ReadTgaPixel(
                source,
                iCursor,
                nPixelDepth,
                bGrayscale,
                rgba ) ) {
            return image_codec_status_t::MALFORMED_DATA;
        }

        for ( u32 iPacketPixel = 0u;
              iPacketPixel < nPacketPixels;
              ++iPacketPixel ) {
            if ( !bRunPacket && !ImageCodec_ReadTgaPixel(
                    source,
                    iCursor,
                    nPixelDepth,
                    bGrayscale,
                    rgba ) ) {
                return image_codec_status_t::MALFORMED_DATA;
            }
            ImageCodec_WriteTgaPixel(
                view,
                iPixel++,
                bTopOrigin,
                bRightOrigin,
                rgba );
        }
    }

    return image_codec_status_t::OK;
}

} // namespace

image_file_format_t ImageCodec_FormatFromPath(
    string_view_t path ) noexcept
{
    if ( !StringView_IsValid( path ) ) {
        return image_file_format_t::UNKNOWN;
    }
    if ( StringPath_HasExtension( path, CodecText( ".png" ), CY_TRUE ) ) {
        return image_file_format_t::PNG;
    }
    if ( StringPath_HasExtension( path, CodecText( ".jpg" ), CY_TRUE ) ||
         StringPath_HasExtension( path, CodecText( ".jpeg" ), CY_TRUE ) ) {
        return image_file_format_t::JPEG;
    }
    if ( StringPath_HasExtension( path, CodecText( ".tga" ), CY_TRUE ) ) {
        return image_file_format_t::TGA;
    }
    if ( StringPath_HasExtension( path, CodecText( ".exr" ), CY_TRUE ) ) {
        return image_file_format_t::EXR;
    }
    return image_file_format_t::UNKNOWN;
}

image_file_format_t ImageCodec_DetectFormat(
    binary_block_t source,
    image_file_format_t formatHint ) noexcept
{
    if ( !BinaryBlock_IsValid( source ) || source.cbSize == 0u ||
         !ImageCodec_IsFormatValid( formatHint ) ) {
        return image_file_format_t::UNKNOWN;
    }

    // Reliable signatures override the caller hint. TGA deliberately falls
    // back to its path-derived hint because the format has no unique magic.
    constexpr byte PNG_MAGIC[]{ 0x89u, 0x50u, 0x4Eu, 0x47u,
                                0x0Du, 0x0Au, 0x1Au, 0x0Au };
    constexpr byte EXR_MAGIC[]{ 0x76u, 0x2Fu, 0x31u, 0x01u };
    if ( source.cbSize >= sizeof( PNG_MAGIC ) &&
         Cy_MemCompare( source.pData, PNG_MAGIC, sizeof( PNG_MAGIC ) ) == 0 ) {
        return image_file_format_t::PNG;
    }
    if ( source.cbSize >= 3u && source.pData[0] == 0xFFu &&
         source.pData[1] == 0xD8u && source.pData[2] == 0xFFu ) {
        return image_file_format_t::JPEG;
    }
    if ( source.cbSize >= sizeof( EXR_MAGIC ) &&
         Cy_MemCompare( source.pData, EXR_MAGIC, sizeof( EXR_MAGIC ) ) == 0 ) {
        return image_file_format_t::EXR;
    }
    return formatHint == image_file_format_t::TGA
        ? image_file_format_t::TGA
        : image_file_format_t::UNKNOWN;
}

image_decode_result_t ImageCodec_Decode(
    binary_block_t source,
    const allocator_t *pAllocator,
    const image_decode_options_t &options,
    image_surface_t *pSurfaceOut ) noexcept
{
    image_decode_result_t result{};
    if ( pSurfaceOut == nullptr || !Allocator_IsValid( pAllocator ) ||
         !BinaryBlock_IsValid( source ) ) {
        result.status = image_codec_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !ImageSurface_IsEmpty( pSurfaceOut ) ) {
        result.status = image_codec_status_t::DESTINATION_NOT_EMPTY;
        return result;
    }
    if ( source.cbSize == 0u ) {
        result.status = image_codec_status_t::EMPTY_SOURCE;
        return result;
    }
    if ( !ImageCodec_AreOptionsValid( options ) ) {
        result.status = image_codec_status_t::INVALID_OPTIONS;
        return result;
    }

    result.sourceFormat = ImageCodec_DetectFormat(
        source,
        options.formatHint );
    if ( result.sourceFormat == image_file_format_t::UNKNOWN ) {
        result.status = image_codec_status_t::UNKNOWN_FORMAT;
        return result;
    }

    // Decode into temporary ownership. The caller's destination remains empty
    // and reusable if any codec, validation, or allocation step fails.
    image_surface_t pending{};
    switch ( result.sourceFormat ) {
        case image_file_format_t::PNG:
            result.status = ImageCodec_DecodePng(
                source, pAllocator, options, pending );
            break;
        case image_file_format_t::JPEG:
            result.status = ImageCodec_DecodeJpeg(
                source, pAllocator, options, pending );
            break;
        case image_file_format_t::TGA:
            result.status = ImageCodec_DecodeTga(
                source, pAllocator, options, pending );
            break;
        case image_file_format_t::EXR:
            result.status = ImageCodec_DecodeExr(
                source, pAllocator, options, pending );
            break;
        default:
            result.status = image_codec_status_t::UNKNOWN_FORMAT;
            break;
    }

    if ( result.status != image_codec_status_t::OK ) {
        return result;
    }
    if ( !ImageSurface_Move( pSurfaceOut, &pending ) ) {
        result.status = image_codec_status_t::ALLOCATION_FAILED;
        return result;
    }

    result.desc = pSurfaceOut->desc;
    result.cbDecoded = ImageSurface_GetByteSize( pSurfaceOut );
    return result;
}

image_codec_status_t ImageCodec_EncodePng(
    const const_image_view_t &source,
    const allocator_t *pAllocator,
    blob_t *pEncodedOut ) noexcept
{
    if ( pEncodedOut == nullptr || !Allocator_IsValid( pAllocator ) ||
         ImageView_Validate( source ) != image_view_status_t::OK ) {
        return image_codec_status_t::INVALID_ARGUMENT;
    }
    if ( pEncodedOut->pData != nullptr || pEncodedOut->cbSize != 0u ||
         pEncodedOut->cbCapacity != 0u || pEncodedOut->pAllocator != nullptr ) {
        return image_codec_status_t::DESTINATION_NOT_EMPTY;
    }
    if ( source.desc.pixelFormat != image_pixel_format_t::RGBA8_UNORM ||
         source.desc.extent.nDepth != 1u ) {
        return image_codec_status_t::UNSUPPORTED_PIXEL_FORMAT;
    }
    if ( source.cbRowPitch >
         static_cast<usize>( std::numeric_limits<png_int_32>::max() ) ) {
        return image_codec_status_t::SIZE_LIMIT_EXCEEDED;
    }

    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    png.width = source.desc.extent.nWidth;
    png.height = source.desc.extent.nHeight;
    png.format = PNG_FORMAT_RGBA;

    // libpng uses a sizing pass followed by the actual write. Keeping both in a
    // temporary blob makes publication transactional.
    png_alloc_size_t cbEncoded = 0u;
    if ( !png_image_write_to_memory(
             &png,
             nullptr,
             &cbEncoded,
             0,
             source.pixels.pData,
             static_cast<png_int_32>( source.cbRowPitch ),
             nullptr ) ) {
        return image_codec_status_t::ENCODE_FAILED;
    }

    blob_t pending{};
    if ( !Blob_Init( &pending, pAllocator ) ||
         !Blob_Resize( &pending, static_cast<usize>( cbEncoded ) ) ) {
        return image_codec_status_t::ALLOCATION_FAILED;
    }
    if ( !png_image_write_to_memory(
             &png,
             pending.pData,
             &cbEncoded,
             0,
             source.pixels.pData,
             static_cast<png_int_32>( source.cbRowPitch ),
             nullptr ) ) {
        return image_codec_status_t::ENCODE_FAILED;
    }
    if ( pending.cbSize != static_cast<usize>( cbEncoded ) &&
         !Blob_Resize( &pending, static_cast<usize>( cbEncoded ) ) ) {
        return image_codec_status_t::ALLOCATION_FAILED;
    }

    Blob_Move( pEncodedOut, &pending );
    return image_codec_status_t::OK;
}

const char *ImageCodec_FormatName( image_file_format_t format ) noexcept
{
    switch ( format ) {
        case image_file_format_t::PNG:  return "PNG";
        case image_file_format_t::JPEG: return "JPEG";
        case image_file_format_t::TGA:  return "TGA";
        case image_file_format_t::EXR:  return "EXR";
        default:                        return "UNKNOWN";
    }
}

const char *ImageCodec_StatusName( image_codec_status_t status ) noexcept
{
    switch ( status ) {
        case image_codec_status_t::OK:                       return "OK";
        case image_codec_status_t::INVALID_ARGUMENT:         return "INVALID_ARGUMENT";
        case image_codec_status_t::INVALID_OPTIONS:          return "INVALID_OPTIONS";
        case image_codec_status_t::EMPTY_SOURCE:             return "EMPTY_SOURCE";
        case image_codec_status_t::DESTINATION_NOT_EMPTY:    return "DESTINATION_NOT_EMPTY";
        case image_codec_status_t::UNKNOWN_FORMAT:           return "UNKNOWN_FORMAT";
        case image_codec_status_t::MALFORMED_DATA:           return "MALFORMED_DATA";
        case image_codec_status_t::UNSUPPORTED_ENCODING:     return "UNSUPPORTED_ENCODING";
        case image_codec_status_t::INVALID_DIMENSIONS:       return "INVALID_DIMENSIONS";
        case image_codec_status_t::SIZE_LIMIT_EXCEEDED:      return "SIZE_LIMIT_EXCEEDED";
        case image_codec_status_t::NON_FINITE_COMPONENT:     return "NON_FINITE_COMPONENT";
        case image_codec_status_t::ALLOCATION_FAILED:        return "ALLOCATION_FAILED";
        case image_codec_status_t::UNSUPPORTED_PIXEL_FORMAT: return "UNSUPPORTED_PIXEL_FORMAT";
        case image_codec_status_t::ENCODE_FAILED:            return "ENCODE_FAILED";
        default:                                              return "UNKNOWN_IMAGE_CODEC_STATUS";
    }
}

} // namespace cypher::common
