//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageConvert.cpp
//  Purpose: Implements allocation-free conversion between CPU image formats.
//  Details: Portable scalar kernels provide the correctness baseline for format,
//           sRGB, alpha, and channel conversion before architecture dispatch.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageConvert.h"

#include "CypherCommon_Color.h"
#include "CypherCommon_MemoryOps.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>

namespace cypher::common
{

namespace
{

constexpr f32 kImageConvertUnorm8Scale = 1.0f / 255.0f;     // Byte UNORM decode scale.
constexpr f32 kImageConvertUnorm16Scale = 1.0f / 65535.0f; // Word UNORM decode scale.
constexpr f32 kImageConvertSrgbToLinearThreshold = 0.04045f; // IEC sRGB decode breakpoint.
constexpr f32 kImageConvertLinearToSrgbThreshold = 0.0031308f; // IEC sRGB encode breakpoint.
constexpr usize kImageConvertSrgbEncodeBucketCount = 4096u; // 4 KiB exact-output accelerator.

static_assert( sizeof( f32 ) == sizeof( u32 ),
               "Image half conversion requires binary32 f32 storage." );

bool_t ImageConvert_RegionHasVolume(
    const image_region_t &region ) noexcept
{
    return region.extent.nWidth > 0u &&
           region.extent.nHeight > 0u &&
           region.extent.nDepth > 0u;
}

// Subtraction-based checks avoid wrapping untrusted origin + extent values.
bool_t ImageConvert_RegionFits(
    const image_desc_t &desc,
    const image_region_t &region ) noexcept
{
    if ( !ImageConvert_RegionHasVolume( region ) ||
         region.origin.iColumn >= desc.extent.nWidth ||
         region.origin.iRow >= desc.extent.nHeight ||
         region.origin.iSlice >= desc.extent.nDepth ) {
        return CY_FALSE;
    }

    return region.extent.nWidth <=
               desc.extent.nWidth - region.origin.iColumn &&
           region.extent.nHeight <=
               desc.extent.nHeight - region.origin.iRow &&
           region.extent.nDepth <=
               desc.extent.nDepth - region.origin.iSlice;
}

bool_t ImageConvert_ExtentsEqual(
    const image_extent_t &left,
    const image_extent_t &right ) noexcept
{
    return left.nWidth == right.nWidth &&
           left.nHeight == right.nHeight &&
           left.nDepth == right.nDepth;
}

bool_t ImageConvert_OriginsEqual(
    const image_origin_t &left,
    const image_origin_t &right ) noexcept
{
    return left.iColumn == right.iColumn &&
           left.iRow == right.iRow &&
           left.iSlice == right.iSlice;
}

bool_t ImageConvert_DescriptorsHaveSameSemantics(
    const image_desc_t &left,
    const image_desc_t &right ) noexcept
{
    return left.pixelFormat == right.pixelFormat &&
           left.colorSpace == right.colorSpace &&
           left.alphaMode == right.alphaMode;
}

byte *ImageConvert_GetRowUnchecked(
    const image_view_t &view,
    u32 iRow,
    u32 iSlice ) noexcept
{
    return view.pixels.pData +
           static_cast<usize>( iSlice ) * view.cbSlicePitch +
           static_cast<usize>( iRow ) * view.cbRowPitch;
}

const byte *ImageConvert_GetRowUnchecked(
    const const_image_view_t &view,
    u32 iRow,
    u32 iSlice ) noexcept
{
    return view.pixels.pData +
           static_cast<usize>( iSlice ) * view.cbSlicePitch +
           static_cast<usize>( iRow ) * view.cbRowPitch;
}

// IEEE binary16 is decoded explicitly because C++20 does not provide a portable
// half type. Unaligned component storage is read through memcpy at the call site.
f32 ImageConvert_HalfToFloat( u16 half ) noexcept
{
    const u32 sign = static_cast<u32>( half & 0x8000u ) << 16u;
    u32 exponent = static_cast<u32>( half >> 10u ) & 0x1Fu;
    u32 mantissa = static_cast<u32>( half & 0x03FFu );
    u32 bits = 0u;

    if ( exponent == 0u ) {
        if ( mantissa == 0u ) {
            bits = sign;
        } else {
            // Normalize a half subnormal before rebiasing it to binary32.
            u32 floatExponent = 127u - 14u;
            while ( ( mantissa & 0x0400u ) == 0u ) {
                mantissa <<= 1u;
                --floatExponent;
            }
            mantissa &= 0x03FFu;
            bits = sign | ( floatExponent << 23u ) | ( mantissa << 13u );
        }
    } else if ( exponent == 0x1Fu ) {
        bits = sign | 0x7F800000u | ( mantissa << 13u );
    } else {
        exponent += 127u - 15u;
        bits = sign | ( exponent << 23u ) | ( mantissa << 13u );
    }

    return std::bit_cast<f32>( bits );
}

// Converts binary32 to binary16 with round-to-nearest, ties-to-even. Overflow
// becomes infinity, underflow becomes a half subnormal or signed zero, and NaN
// payloads remain non-zero and are made quiet.
u16 ImageConvert_FloatToHalf( f32 value ) noexcept
{
    const u32 bits = std::bit_cast<u32>( value );
    const u16 sign = static_cast<u16>( ( bits >> 16u ) & 0x8000u );
    const u32 exponent = ( bits >> 23u ) & 0xFFu;
    u32 mantissa = bits & 0x007FFFFFu;

    if ( exponent == 0xFFu ) {
        if ( mantissa == 0u ) {
            return static_cast<u16>( sign | 0x7C00u );
        }
        u16 payload = static_cast<u16>( mantissa >> 13u );
        payload = static_cast<u16>( payload | 0x0200u );
        return static_cast<u16>( sign | 0x7C00u | payload );
    }

    const i32 halfExponent = static_cast<i32>( exponent ) - 127 + 15;
    if ( halfExponent >= 31 ) {
        return static_cast<u16>( sign | 0x7C00u );
    }

    if ( halfExponent <= 0 ) {
        if ( halfExponent < -10 ) {
            return sign;
        }

        mantissa |= 0x00800000u;
        const u32 nShift = static_cast<u32>( 14 - halfExponent );
        u32 halfMantissa = mantissa >> nShift;
        const u32 nRemainderMask = ( 1u << nShift ) - 1u;
        const u32 nRemainder = mantissa & nRemainderMask;
        const u32 nHalfway = 1u << ( nShift - 1u );
        if ( nRemainder > nHalfway ||
             ( nRemainder == nHalfway && ( halfMantissa & 1u ) != 0u ) ) {
            ++halfMantissa;
        }
        return static_cast<u16>( sign | halfMantissa );
    }

    u32 halfMantissa = mantissa >> 13u;
    const u32 nRemainder = mantissa & 0x1FFFu;
    if ( nRemainder > 0x1000u ||
         ( nRemainder == 0x1000u && ( halfMantissa & 1u ) != 0u ) ) {
        ++halfMantissa;
    }

    u32 adjustedExponent = static_cast<u32>( halfExponent );
    if ( halfMantissa == 0x0400u ) {
        halfMantissa = 0u;
        ++adjustedExponent;
        if ( adjustedExponent >= 31u ) {
            return static_cast<u16>( sign | 0x7C00u );
        }
    }

    return static_cast<u16>(
        sign | ( adjustedExponent << 10u ) | halfMantissa );
}

f32 ImageConvert_LoadComponent(
    const byte *pComponent,
    image_numeric_type_t numericType,
    u8 cbComponent ) noexcept
{
    // Component access uses memcpy because image rows guarantee byte access but
    // do not promise native u16 or f32 alignment for every channel.
    if ( numericType == image_numeric_type_t::UNORM ) {
        if ( cbComponent == 1u ) {
            return static_cast<f32>( pComponent[0] ) *
                   kImageConvertUnorm8Scale;
        }

        u16 value = 0u;
        std::memcpy( &value, pComponent, sizeof( value ) );
        return static_cast<f32>( value ) * kImageConvertUnorm16Scale;
    }

    if ( cbComponent == 2u ) {
        u16 value = 0u;
        std::memcpy( &value, pComponent, sizeof( value ) );
        return ImageConvert_HalfToFloat( value );
    }

    f32 value = 0.0f;
    std::memcpy( &value, pComponent, sizeof( value ) );
    return value;
}

f32 ImageConvert_ClampUnorm( f32 value ) noexcept
{
    if ( std::isnan( value ) || value <= 0.0f ) {
        return 0.0f;
    }
    return value >= 1.0f ? 1.0f : value;
}

void ImageConvert_StoreComponent(
    byte *pComponent,
    f32 value,
    image_numeric_type_t numericType,
    u8 cbComponent ) noexcept
{
    // UNORM conversion clamps untrusted float input and rounds to the nearest
    // representable integer. Float destinations preserve their wider range.
    if ( numericType == image_numeric_type_t::UNORM ) {
        const f32 clamped = ImageConvert_ClampUnorm( value );
        if ( cbComponent == 1u ) {
            pComponent[0] = static_cast<byte>( clamped * 255.0f + 0.5f );
            return;
        }

        const u16 encoded = static_cast<u16>( clamped * 65535.0f + 0.5f );
        std::memcpy( pComponent, &encoded, sizeof( encoded ) );
        return;
    }

    if ( cbComponent == 2u ) {
        const u16 encoded = ImageConvert_FloatToHalf( value );
        std::memcpy( pComponent, &encoded, sizeof( encoded ) );
        return;
    }

    std::memcpy( pComponent, &value, sizeof( value ) );
}

f32 ImageConvert_SrgbToLinear( f32 value ) noexcept
{
    return value <= kImageConvertSrgbToLinearThreshold
        ? value / 12.92f
        : std::pow( ( value + 0.055f ) / 1.055f, 2.4f );
}

f32 ImageConvert_LinearToSrgb( f32 value ) noexcept
{
    return value <= kImageConvertLinearToSrgbThreshold
        ? value * 12.92f
        : 1.055f * std::pow( value, 1.0f / 2.4f ) - 0.055f;
}

byte ImageConvert_LinearToSrgb8Reference( f32 value ) noexcept
{
    const f32 clamped = ImageConvert_ClampUnorm( value );
    const f32 encoded = ImageConvert_LinearToSrgb( clamped );
    return static_cast<byte>( encoded * 255.0f + 0.5f );
}

// Byte sRGB is common in authored textures. Decoding through this immutable table
// removes three pow calls per pixel while producing the same transfer values as
// the generic scalar path.
const std::array<f32, 256u> &ImageConvert_GetSrgb8DecodeTable() noexcept
{
    static const std::array<f32, 256u> table = []() {
        std::array<f32, 256u> values{};
        for ( usize iValue = 0u; iValue < values.size(); ++iValue ) {
            values[iValue] = ImageConvert_SrgbToLinear(
                static_cast<f32>( iValue ) * kImageConvertUnorm8Scale );
        }
        return values;
    }();
    return table;
}

// Quantized sRGB output only changes at 255 linear-light boundaries. Searching
// those boundaries is exact for byte output and substantially cheaper than pow.
const std::array<f32, 255u> &ImageConvert_GetSrgb8EncodeBoundaries() noexcept
{
    static const std::array<f32, 255u> boundaries = []() {
        std::array<f32, 255u> values{};
        for ( usize iBoundary = 0u;
              iBoundary < values.size();
              ++iBoundary ) {
            // Positive IEEE binary32 bit patterns are ordered exactly like their
            // numeric values. Find the first representable input for which the
            // established reference quantizer advances to the next byte code.
            u32 iFirstBits = 0u;
            u32 iLastBits = std::bit_cast<u32>( 1.0f );
            while ( iFirstBits < iLastBits ) {
                const u32 iMiddleBits =
                    iFirstBits + ( iLastBits - iFirstBits ) / 2u;
                const byte encoded = ImageConvert_LinearToSrgb8Reference(
                    std::bit_cast<f32>( iMiddleBits ) );
                if ( encoded <= static_cast<byte>( iBoundary ) ) {
                    iFirstBits = iMiddleBits + 1u;
                } else {
                    iLastBits = iMiddleBits;
                }
            }
            values[iBoundary] = std::bit_cast<f32>( iFirstBits );
        }
        return values;
    }();
    return boundaries;
}

// A 4 KiB accelerator maps a linear-light interval to the code valid at that
// interval's lower edge. One exact boundary correction then handles the rest of
// the bucket, avoiding an eight-step binary search for every color channel.
const std::array<byte, kImageConvertSrgbEncodeBucketCount> &
ImageConvert_GetSrgb8EncodeBuckets() noexcept
{
    static const std::array<byte, kImageConvertSrgbEncodeBucketCount> buckets =
        []() {
            std::array<byte, kImageConvertSrgbEncodeBucketCount> values{};
            const std::array<f32, 255u> &boundaries =
                ImageConvert_GetSrgb8EncodeBoundaries();
            usize iCode = 0u;
            for ( usize iBucket = 0u;
                  iBucket < values.size();
                  ++iBucket ) {
                const f32 lowerEdge =
                    static_cast<f32>( iBucket ) /
                    static_cast<f32>( values.size() );
                while ( iCode < boundaries.size() &&
                        lowerEdge >= boundaries[iCode] ) {
                    ++iCode;
                }
                values[iBucket] = static_cast<byte>( iCode );
            }
            return values;
        }();
    return buckets;
}

byte ImageConvert_LinearToSrgb8( f32 value ) noexcept
{
    if ( std::isnan( value ) || value <= 0.0f ) {
        return 0u;
    }
    if ( value >= 1.0f ) {
        return 255u;
    }

    const std::array<f32, 255u> &boundaries =
        ImageConvert_GetSrgb8EncodeBoundaries();
    const std::array<byte, kImageConvertSrgbEncodeBucketCount> &buckets =
        ImageConvert_GetSrgb8EncodeBuckets();
    // The bucket gives a near-exact code. At most a small boundary correction
    // is needed to preserve bit-for-bit reference quantization.
    const usize iBucket = static_cast<usize>(
        value * static_cast<f32>( kImageConvertSrgbEncodeBucketCount ) );
    usize iCode = buckets[iBucket];

    while ( iCode < boundaries.size() && value >= boundaries[iCode] ) {
        ++iCode;
    }
    while ( iCode > 0u && value < boundaries[iCode - 1u] ) {
        --iCode;
    }
    return static_cast<byte>( iCode );
}

colorf_t ImageConvert_LoadCanonicalPixel(
    const byte *pPixel,
    const image_desc_t &desc,
    const image_format_info_t &formatInfo ) noexcept
{
    // Every format enters the generic kernel as straight-alpha linear RGBA.
    // Missing channels retain the canonical zero/zero/zero/one defaults.
    colorf_t color{ 0.0f, 0.0f, 0.0f, 1.0f };
    f32 *pChannels[4u]{ &color.r, &color.g, &color.b, &color.a };

    for ( u8 iChannel = 0u;
          iChannel < formatInfo.cChannels;
          ++iChannel ) {
        *pChannels[iChannel] = ImageConvert_LoadComponent(
            pPixel + static_cast<usize>( iChannel ) * formatInfo.cbComponent,
            formatInfo.numericType,
            formatInfo.cbComponent );
    }

    // Color-space transfer always precedes alpha disassociation. A premultiplied
    // sRGB pixel therefore returns to premultiplied linear light before division.
    if ( desc.colorSpace == image_color_space_t::SRGB ) {
        color.r = ImageConvert_SrgbToLinear( color.r );
        color.g = ImageConvert_SrgbToLinear( color.g );
        color.b = ImageConvert_SrgbToLinear( color.b );
    }

    if ( desc.alphaMode == image_alpha_mode_t::NONE ) {
        color.a = 1.0f;
    } else if ( desc.alphaMode == image_alpha_mode_t::PREMULTIPLIED ) {
        color = Color_UnpremultiplyAlpha( color );
    }

    return color;
}

f32 ImageConvert_SelectChannel(
    const colorf_t &color,
    image_channel_t channel ) noexcept
{
    switch ( channel ) {
        case image_channel_t::ZERO:  return 0.0f;
        case image_channel_t::ONE:   return 1.0f;
        case image_channel_t::RED:   return color.r;
        case image_channel_t::GREEN: return color.g;
        case image_channel_t::BLUE:  return color.b;
        case image_channel_t::ALPHA: return color.a;
        default:                     return 0.0f;
    }
}

colorf_t ImageConvert_ApplySwizzle(
    const colorf_t &color,
    const image_swizzle_t &swizzle ) noexcept
{
    // Apply mapping after decode and alpha disassociation so constants and
    // selected channels all share one well-defined semantic space.
    return {
        ImageConvert_SelectChannel( color, swizzle.red ),
        ImageConvert_SelectChannel( color, swizzle.green ),
        ImageConvert_SelectChannel( color, swizzle.blue ),
        ImageConvert_SelectChannel( color, swizzle.alpha )
    };
}

void ImageConvert_StoreCanonicalPixel(
    byte *pPixel,
    colorf_t color,
    const image_desc_t &desc,
    const image_format_info_t &formatInfo ) noexcept
{
    // Destination policy is the reverse of canonical loading: associate alpha,
    // encode RGB transfer, then quantize physical components.
    if ( desc.alphaMode == image_alpha_mode_t::NONE ) {
        color.a = 1.0f;
    } else if ( desc.alphaMode == image_alpha_mode_t::PREMULTIPLIED ) {
        color = Color_PremultiplyAlpha( color );
    }

    // Alpha is linear and is never passed through the sRGB transfer function.
    if ( desc.colorSpace == image_color_space_t::SRGB ) {
        color.r = ImageConvert_LinearToSrgb( color.r );
        color.g = ImageConvert_LinearToSrgb( color.g );
        color.b = ImageConvert_LinearToSrgb( color.b );
    }

    const f32 channels[4u]{ color.r, color.g, color.b, color.a };
    for ( u8 iChannel = 0u;
          iChannel < formatInfo.cChannels;
          ++iChannel ) {
        ImageConvert_StoreComponent(
            pPixel + static_cast<usize>( iChannel ) * formatInfo.cbComponent,
            channels[iChannel],
            formatInfo.numericType,
            formatInfo.cbComponent );
    }
}

bool_t ImageConvert_IsExactInPlaceRegion(
    const image_view_t &destination,
    const image_origin_t &destinationOrigin,
    const const_image_view_t &source,
    const image_region_t &sourceRegion,
    usize cbDestinationPixel,
    usize cbSourcePixel ) noexcept
{
    // Equal pixel byte widths are required even when semantic conversion is
    // requested; otherwise writing one destination pixel could clobber the next
    // unread source pixel.
    return destination.pixels.pData == source.pixels.pData &&
           destination.pixels.nCount == source.pixels.cbSize &&
           destination.cbRowPitch == source.cbRowPitch &&
           destination.cbSlicePitch == source.cbSlicePitch &&
           ImageConvert_ExtentsEqual(
               destination.desc.extent,
               source.desc.extent ) &&
           ImageConvert_OriginsEqual(
               destinationOrigin,
               sourceRegion.origin ) &&
           cbDestinationPixel == cbSourcePixel;
}

void ImageConvert_CopyRegionUnchecked(
    const image_view_t &destination,
    const image_origin_t &destinationOrigin,
    const const_image_view_t &source,
    const image_region_t &sourceRegion,
    usize cbPixel ) noexcept
{
    const usize cbRow =
        static_cast<usize>( sourceRegion.extent.nWidth ) * cbPixel;
    const usize iSourceColumn =
        static_cast<usize>( sourceRegion.origin.iColumn ) * cbPixel;
    const usize iDestinationColumn =
        static_cast<usize>( destinationOrigin.iColumn ) * cbPixel;

    for ( u32 iSlice = 0u;
          iSlice < sourceRegion.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u;
              iRow < sourceRegion.extent.nHeight;
              ++iRow ) {
            const byte *pSource = ImageConvert_GetRowUnchecked(
                source,
                sourceRegion.origin.iRow + iRow,
                sourceRegion.origin.iSlice + iSlice ) + iSourceColumn;
            byte *pDestination = ImageConvert_GetRowUnchecked(
                destination,
                destinationOrigin.iRow + iRow,
                destinationOrigin.iSlice + iSlice ) + iDestinationColumn;
            Cy_MemCopy( pDestination, pSource, cbRow );
        }
    }
}

byte ImageConvert_SelectByteChannel(
    const byte source[4u],
    image_channel_t channel ) noexcept
{
    switch ( channel ) {
        case image_channel_t::ZERO:  return 0u;
        case image_channel_t::ONE:   return 255u;
        case image_channel_t::RED:   return source[0];
        case image_channel_t::GREEN: return source[1];
        case image_channel_t::BLUE:  return source[2];
        case image_channel_t::ALPHA: return source[3];
        default:                     return 0u;
    }
}

void ImageConvert_Rgba8SwizzleUnchecked(
    const image_view_t &destination,
    const image_origin_t &destinationOrigin,
    const const_image_view_t &source,
    const image_region_t &sourceRegion,
    const image_swizzle_t &swizzle ) noexcept
{
    for ( u32 iSlice = 0u;
          iSlice < sourceRegion.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u;
              iRow < sourceRegion.extent.nHeight;
              ++iRow ) {
            const byte *pSource = ImageConvert_GetRowUnchecked(
                source,
                sourceRegion.origin.iRow + iRow,
                sourceRegion.origin.iSlice + iSlice ) +
                static_cast<usize>( sourceRegion.origin.iColumn ) * 4u;
            byte *pDestination = ImageConvert_GetRowUnchecked(
                destination,
                destinationOrigin.iRow + iRow,
                destinationOrigin.iSlice + iSlice ) +
                static_cast<usize>( destinationOrigin.iColumn ) * 4u;

            for ( u32 iColumn = 0u;
                  iColumn < sourceRegion.extent.nWidth;
                  ++iColumn ) {
                // Read all channels before writing so exact in-place swizzles are safe.
                const byte sourcePixel[4u]{
                    pSource[0], pSource[1], pSource[2], pSource[3]
                };
                pDestination[0] = ImageConvert_SelectByteChannel(
                    sourcePixel, swizzle.red );
                pDestination[1] = ImageConvert_SelectByteChannel(
                    sourcePixel, swizzle.green );
                pDestination[2] = ImageConvert_SelectByteChannel(
                    sourcePixel, swizzle.blue );
                pDestination[3] = ImageConvert_SelectByteChannel(
                    sourcePixel, swizzle.alpha );
                pSource += 4u;
                pDestination += 4u;
            }
        }
    }
}

void ImageConvert_Unorm8ToUnorm16Unchecked(
    const image_view_t &destination,
    const image_origin_t &destinationOrigin,
    const const_image_view_t &source,
    const image_region_t &sourceRegion,
    u8 cChannels ) noexcept
{
    const usize cbSourcePixel = cChannels;
    const usize cbDestinationPixel = static_cast<usize>( cChannels ) * 2u;
    for ( u32 iSlice = 0u;
          iSlice < sourceRegion.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u;
              iRow < sourceRegion.extent.nHeight;
              ++iRow ) {
            const byte *pSource = ImageConvert_GetRowUnchecked(
                source,
                sourceRegion.origin.iRow + iRow,
                sourceRegion.origin.iSlice + iSlice ) +
                static_cast<usize>( sourceRegion.origin.iColumn ) *
                    cbSourcePixel;
            byte *pDestination = ImageConvert_GetRowUnchecked(
                destination,
                destinationOrigin.iRow + iRow,
                destinationOrigin.iSlice + iSlice ) +
                static_cast<usize>( destinationOrigin.iColumn ) *
                    cbDestinationPixel;

            for ( u32 iColumn = 0u;
                  iColumn < sourceRegion.extent.nWidth;
                  ++iColumn ) {
                for ( u8 iChannel = 0u;
                      iChannel < cChannels;
                      ++iChannel ) {
                    // Multiplication by 257 duplicates the byte into both halves
                    // and maps every 8-bit UNORM endpoint exactly into 16 bits.
                    const u16 value = static_cast<u16>(
                        static_cast<u16>( pSource[iChannel] ) * 257u );
                    std::memcpy(
                        pDestination + static_cast<usize>( iChannel ) * 2u,
                        &value,
                        sizeof( value ) );
                }
                pSource += cbSourcePixel;
                pDestination += cbDestinationPixel;
            }
        }
    }
}

void ImageConvert_Rgba8SrgbToRgba32LinearUnchecked(
    const image_view_t &destination,
    const image_origin_t &destinationOrigin,
    const const_image_view_t &source,
    const image_region_t &sourceRegion ) noexcept
{
    // Texture authoring and mip generation use this path heavily. RGB uses the
    // immutable sRGB table while alpha remains linearly normalized.
    const std::array<f32, 256u> &decode =
        ImageConvert_GetSrgb8DecodeTable();
    for ( u32 iSlice = 0u;
          iSlice < sourceRegion.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u;
              iRow < sourceRegion.extent.nHeight;
              ++iRow ) {
            const byte *pSource = ImageConvert_GetRowUnchecked(
                source,
                sourceRegion.origin.iRow + iRow,
                sourceRegion.origin.iSlice + iSlice ) +
                static_cast<usize>( sourceRegion.origin.iColumn ) * 4u;
            byte *pDestination = ImageConvert_GetRowUnchecked(
                destination,
                destinationOrigin.iRow + iRow,
                destinationOrigin.iSlice + iSlice ) +
                static_cast<usize>( destinationOrigin.iColumn ) * 16u;

            for ( u32 iColumn = 0u;
                  iColumn < sourceRegion.extent.nWidth;
                  ++iColumn ) {
                const f32 value[4u]{
                    decode[pSource[0]],
                    decode[pSource[1]],
                    decode[pSource[2]],
                    static_cast<f32>( pSource[3] ) * kImageConvertUnorm8Scale
                };
                std::memcpy( pDestination, value, sizeof( value ) );
                pSource += 4u;
                pDestination += 16u;
            }
        }
    }
}

void ImageConvert_Rgba32LinearToRgba8SrgbUnchecked(
    const image_view_t &destination,
    const image_origin_t &destinationOrigin,
    const const_image_view_t &source,
    const image_region_t &sourceRegion ) noexcept
{
    // This is the inverse publication path for display images and cooked
    // uncompressed texture data.
    for ( u32 iSlice = 0u;
          iSlice < sourceRegion.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u;
              iRow < sourceRegion.extent.nHeight;
              ++iRow ) {
            const byte *pSource = ImageConvert_GetRowUnchecked(
                source,
                sourceRegion.origin.iRow + iRow,
                sourceRegion.origin.iSlice + iSlice ) +
                static_cast<usize>( sourceRegion.origin.iColumn ) * 16u;
            byte *pDestination = ImageConvert_GetRowUnchecked(
                destination,
                destinationOrigin.iRow + iRow,
                destinationOrigin.iSlice + iSlice ) +
                static_cast<usize>( destinationOrigin.iColumn ) * 4u;

            for ( u32 iColumn = 0u;
                  iColumn < sourceRegion.extent.nWidth;
                  ++iColumn ) {
                f32 value[4u]{};
                std::memcpy( value, pSource, sizeof( value ) );
                pDestination[0] = ImageConvert_LinearToSrgb8( value[0] );
                pDestination[1] = ImageConvert_LinearToSrgb8( value[1] );
                pDestination[2] = ImageConvert_LinearToSrgb8( value[2] );
                pDestination[3] = static_cast<byte>(
                    ImageConvert_ClampUnorm( value[3] ) * 255.0f + 0.5f );
                pSource += 16u;
                pDestination += 4u;
            }
        }
    }
}

void ImageConvert_Rgba8PremultiplyUnchecked(
    const image_view_t &destination,
    const image_origin_t &destinationOrigin,
    const const_image_view_t &source,
    const image_region_t &sourceRegion ) noexcept
{
    // Integer arithmetic with a half-denominator bias gives deterministic
    // round-to-nearest results without converting each byte to float.
    for ( u32 iSlice = 0u;
          iSlice < sourceRegion.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u;
              iRow < sourceRegion.extent.nHeight;
              ++iRow ) {
            const byte *pSource = ImageConvert_GetRowUnchecked(
                source,
                sourceRegion.origin.iRow + iRow,
                sourceRegion.origin.iSlice + iSlice ) +
                static_cast<usize>( sourceRegion.origin.iColumn ) * 4u;
            byte *pDestination = ImageConvert_GetRowUnchecked(
                destination,
                destinationOrigin.iRow + iRow,
                destinationOrigin.iSlice + iSlice ) +
                static_cast<usize>( destinationOrigin.iColumn ) * 4u;

            for ( u32 iColumn = 0u;
                  iColumn < sourceRegion.extent.nWidth;
                  ++iColumn ) {
                const u32 alpha = pSource[3];
                pDestination[0] = static_cast<byte>(
                    ( static_cast<u32>( pSource[0] ) * alpha + 127u ) / 255u );
                pDestination[1] = static_cast<byte>(
                    ( static_cast<u32>( pSource[1] ) * alpha + 127u ) / 255u );
                pDestination[2] = static_cast<byte>(
                    ( static_cast<u32>( pSource[2] ) * alpha + 127u ) / 255u );
                pDestination[3] = pSource[3];
                pSource += 4u;
                pDestination += 4u;
            }
        }
    }
}

bool_t ImageConvert_TrySpecializedKernel(
    const image_view_t &destination,
    const image_origin_t &destinationOrigin,
    const const_image_view_t &source,
    const image_region_t &sourceRegion,
    const image_convert_options_t &options,
    const image_format_info_t &sourceInfo,
    const image_format_info_t &destinationInfo ) noexcept
{
    // Keep hot, common conversions here and leave unusual combinations to the
    // canonical scalar path. A kernel must preserve the exact generic semantics.
    const bool_t bIdentitySwizzle =
        ImageConvert_IsIdentitySwizzle( options.swizzle );

    if ( source.desc.pixelFormat == image_pixel_format_t::RGBA8_UNORM &&
         destination.desc.pixelFormat == image_pixel_format_t::RGBA8_UNORM &&
         source.desc.colorSpace == image_color_space_t::LINEAR &&
         destination.desc.colorSpace == image_color_space_t::LINEAR &&
         source.desc.alphaMode == image_alpha_mode_t::STRAIGHT &&
         destination.desc.alphaMode == image_alpha_mode_t::STRAIGHT ) {
        ImageConvert_Rgba8SwizzleUnchecked(
            destination,
            destinationOrigin,
            source,
            sourceRegion,
            options.swizzle );
        return CY_TRUE;
    }

    if ( bIdentitySwizzle &&
         sourceInfo.numericType == image_numeric_type_t::UNORM &&
         destinationInfo.numericType == image_numeric_type_t::UNORM &&
         sourceInfo.cbComponent == 1u &&
         destinationInfo.cbComponent == 2u &&
         sourceInfo.cChannels == destinationInfo.cChannels &&
         source.desc.colorSpace == destination.desc.colorSpace &&
         source.desc.alphaMode == destination.desc.alphaMode &&
         ( !sourceInfo.bHasAlpha ||
           source.desc.alphaMode != image_alpha_mode_t::NONE ) ) {
        ImageConvert_Unorm8ToUnorm16Unchecked(
            destination,
            destinationOrigin,
            source,
            sourceRegion,
            sourceInfo.cChannels );
        return CY_TRUE;
    }

    if ( bIdentitySwizzle &&
         source.desc.pixelFormat == image_pixel_format_t::RGBA8_UNORM &&
         destination.desc.pixelFormat == image_pixel_format_t::RGBA32_FLOAT &&
         source.desc.colorSpace == image_color_space_t::SRGB &&
         destination.desc.colorSpace == image_color_space_t::LINEAR &&
         source.desc.alphaMode == image_alpha_mode_t::STRAIGHT &&
         destination.desc.alphaMode == image_alpha_mode_t::STRAIGHT ) {
        ImageConvert_Rgba8SrgbToRgba32LinearUnchecked(
            destination,
            destinationOrigin,
            source,
            sourceRegion );
        return CY_TRUE;
    }

    if ( bIdentitySwizzle &&
         source.desc.pixelFormat == image_pixel_format_t::RGBA32_FLOAT &&
         destination.desc.pixelFormat == image_pixel_format_t::RGBA8_UNORM &&
         source.desc.colorSpace == image_color_space_t::LINEAR &&
         destination.desc.colorSpace == image_color_space_t::SRGB &&
         source.desc.alphaMode == image_alpha_mode_t::STRAIGHT &&
         destination.desc.alphaMode == image_alpha_mode_t::STRAIGHT ) {
        ImageConvert_Rgba32LinearToRgba8SrgbUnchecked(
            destination,
            destinationOrigin,
            source,
            sourceRegion );
        return CY_TRUE;
    }

    if ( bIdentitySwizzle &&
         source.desc.pixelFormat == image_pixel_format_t::RGBA8_UNORM &&
         destination.desc.pixelFormat == image_pixel_format_t::RGBA8_UNORM &&
         source.desc.colorSpace == image_color_space_t::LINEAR &&
         destination.desc.colorSpace == image_color_space_t::LINEAR &&
         source.desc.alphaMode == image_alpha_mode_t::STRAIGHT &&
         destination.desc.alphaMode == image_alpha_mode_t::PREMULTIPLIED ) {
        ImageConvert_Rgba8PremultiplyUnchecked(
            destination,
            destinationOrigin,
            source,
            sourceRegion );
        return CY_TRUE;
    }

    return CY_FALSE;
}

} // namespace

bool_t ImageConvert_IsSwizzleValid(
    const image_swizzle_t &swizzle ) noexcept
{
    const auto isChannelValid = []( image_channel_t channel ) noexcept {
        return static_cast<u8>( channel ) <
               static_cast<u8>( image_channel_t::COUNT );
    };

    return isChannelValid( swizzle.red ) &&
           isChannelValid( swizzle.green ) &&
           isChannelValid( swizzle.blue ) &&
           isChannelValid( swizzle.alpha );
}

bool_t ImageConvert_IsIdentitySwizzle(
    const image_swizzle_t &swizzle ) noexcept
{
    return swizzle.red == image_channel_t::RED &&
           swizzle.green == image_channel_t::GREEN &&
           swizzle.blue == image_channel_t::BLUE &&
           swizzle.alpha == image_channel_t::ALPHA;
}

image_convert_status_t ImageConvert(
    const image_view_t &destination,
    const const_image_view_t &source,
    const image_convert_options_t &options ) noexcept
{
    if ( ImageView_Validate( destination ) != image_view_status_t::OK ) {
        return image_convert_status_t::INVALID_DESTINATION_VIEW;
    }
    if ( ImageView_Validate( source ) != image_view_status_t::OK ) {
        return image_convert_status_t::INVALID_SOURCE_VIEW;
    }
    if ( !ImageConvert_ExtentsEqual(
             destination.desc.extent,
             source.desc.extent ) ) {
        return image_convert_status_t::EXTENT_MISMATCH;
    }

    return ImageConvert_Region(
        destination,
        {},
        source,
        ImageProcess_FullRegion( source.desc ),
        options );
}

image_convert_status_t ImageConvert_Region(
    const image_view_t &destination,
    const image_origin_t &destinationOrigin,
    const const_image_view_t &source,
    const image_region_t &sourceRegion,
    const image_convert_options_t &options ) noexcept
{
    if ( ImageView_Validate( destination ) != image_view_status_t::OK ) {
        return image_convert_status_t::INVALID_DESTINATION_VIEW;
    }
    if ( ImageView_Validate( source ) != image_view_status_t::OK ) {
        return image_convert_status_t::INVALID_SOURCE_VIEW;
    }
    if ( !ImageConvert_IsSwizzleValid( options.swizzle ) ) {
        return image_convert_status_t::INVALID_SWIZZLE;
    }
    if ( !ImageConvert_RegionHasVolume( sourceRegion ) ) {
        return image_convert_status_t::INVALID_REGION;
    }
    if ( !ImageConvert_RegionFits( source.desc, sourceRegion ) ) {
        return image_convert_status_t::SOURCE_REGION_OUT_OF_BOUNDS;
    }

    // Reuse the same half-open region validation for destination placement.
    const image_region_t destinationRegion{
        destinationOrigin,
        sourceRegion.extent
    };
    if ( !ImageConvert_RegionFits( destination.desc, destinationRegion ) ) {
        return image_convert_status_t::DESTINATION_REGION_OUT_OF_BOUNDS;
    }

    const image_format_info_t *pSourceInfo =
        ImageFormat_GetInfo( source.desc.pixelFormat );
    const image_format_info_t *pDestinationInfo =
        ImageFormat_GetInfo( destination.desc.pixelFormat );
    const usize cbSourcePixel = pSourceInfo->cbPixel;
    const usize cbDestinationPixel = pDestinationInfo->cbPixel;
    const bool_t bExactInPlace = ImageConvert_IsExactInPlaceRegion(
        destination,
        destinationOrigin,
        source,
        sourceRegion,
        cbDestinationPixel,
        cbSourcePixel );

    if ( Cy_MemRangesOverlap(
             destination.pixels.pData,
             destination.pixels.nCount,
             source.pixels.pData,
             source.pixels.cbSize ) &&
         !bExactInPlace ) {
        return image_convert_status_t::OVERLAPPING_MEMORY;
    }

    // Preserve exact bits for a semantic identity conversion. This is both
    // faster and avoids needless float round trips through already quantized data.
    if ( ImageConvert_DescriptorsHaveSameSemantics(
             destination.desc,
             source.desc ) &&
         ImageConvert_IsIdentitySwizzle( options.swizzle ) ) {
        if ( bExactInPlace ) {
            return image_convert_status_t::OK;
        }
        ImageConvert_CopyRegionUnchecked(
            destination,
            destinationOrigin,
            source,
            sourceRegion,
            cbSourcePixel );
        return image_convert_status_t::OK;
    }

    // Specialized kernels are optional accelerators; failure to select one is
    // not an error and falls through to the canonical conversion loop.
    if ( ImageConvert_TrySpecializedKernel(
             destination,
             destinationOrigin,
             source,
             sourceRegion,
             options,
             *pSourceInfo,
             *pDestinationInfo ) ) {
        return image_convert_status_t::OK;
    }

    // Generic conversion order is fixed: decode to straight linear RGBA, apply
    // the channel map, then encode according to destination metadata.
    for ( u32 iSlice = 0u;
          iSlice < sourceRegion.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u;
              iRow < sourceRegion.extent.nHeight;
              ++iRow ) {
            const byte *pSourcePixel = ImageConvert_GetRowUnchecked(
                source,
                sourceRegion.origin.iRow + iRow,
                sourceRegion.origin.iSlice + iSlice ) +
                static_cast<usize>( sourceRegion.origin.iColumn ) *
                    cbSourcePixel;
            byte *pDestinationPixel = ImageConvert_GetRowUnchecked(
                destination,
                destinationOrigin.iRow + iRow,
                destinationOrigin.iSlice + iSlice ) +
                static_cast<usize>( destinationOrigin.iColumn ) *
                    cbDestinationPixel;

            for ( u32 iColumn = 0u;
                  iColumn < sourceRegion.extent.nWidth;
                  ++iColumn ) {
                const colorf_t sourceColor = ImageConvert_LoadCanonicalPixel(
                    pSourcePixel,
                    source.desc,
                    *pSourceInfo );
                const colorf_t destinationColor = ImageConvert_ApplySwizzle(
                    sourceColor,
                    options.swizzle );
                ImageConvert_StoreCanonicalPixel(
                    pDestinationPixel,
                    destinationColor,
                    destination.desc,
                    *pDestinationInfo );

                pSourcePixel += cbSourcePixel;
                pDestinationPixel += cbDestinationPixel;
            }
        }
    }

    return image_convert_status_t::OK;
}

const char *ImageConvert_StatusName(
    image_convert_status_t status ) noexcept
{
    // Status spellings are consumed by tests and editor diagnostics.
    switch ( status ) {
        case image_convert_status_t::OK:
            return "OK";
        case image_convert_status_t::INVALID_SOURCE_VIEW:
            return "INVALID_SOURCE_VIEW";
        case image_convert_status_t::INVALID_DESTINATION_VIEW:
            return "INVALID_DESTINATION_VIEW";
        case image_convert_status_t::INVALID_SWIZZLE:
            return "INVALID_SWIZZLE";
        case image_convert_status_t::INVALID_REGION:
            return "INVALID_REGION";
        case image_convert_status_t::SOURCE_REGION_OUT_OF_BOUNDS:
            return "SOURCE_REGION_OUT_OF_BOUNDS";
        case image_convert_status_t::DESTINATION_REGION_OUT_OF_BOUNDS:
            return "DESTINATION_REGION_OUT_OF_BOUNDS";
        case image_convert_status_t::EXTENT_MISMATCH:
            return "EXTENT_MISMATCH";
        case image_convert_status_t::OVERLAPPING_MEMORY:
            return "OVERLAPPING_MEMORY";
        default:
            return "UNKNOWN_IMAGE_CONVERT_STATUS";
    }
}

} // namespace cypher::common
