//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageResize.cpp
//  Purpose: Implements allocation-free resizing of uncompressed CPU images.
//  Details: Scalar reference kernels support pitched 2D/3D views, preserve
//           padding, and filter straight alpha without introducing color halos.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageResize.h"

#include "CypherCommon_MemoryOps.h"

#include <cmath>
#include <cstring>

namespace cypher::common
{

namespace
{

//===================================================================
// Sampling state used by the general double-precision reference path.

struct image_resize_axis_sample_t {
    u32 indices[2u]{ 0u, 0u }; // Clamped source indices on one axis.
    f64 weights[2u]{ 1.0, 0.0 }; // Normalized contribution of each index.
    u8 cSamples{ 1u }; // One at an edge, otherwise two for linear filtering.
};

struct image_resize_work_pixel_t {
    f64 channels[4u]{ 0.0, 0.0, 0.0, 1.0 }; // Premultiplied linear RGBA accumulator.
};

// Exact 2:1 and 1:2 paths use floats to match their RGBA32 source storage and
// avoid conversion overhead in the dominant mip-generation workload.
struct image_resize_fast_axis_sample_t {
    u32 indices[2u]{ 0u, 0u }; // Clamped source indices on one axis.
    f32 weights[2u]{ 1.0f, 0.0f }; // Float weights for specialized paths.
    u8 cSamples{ 1u }; // Number of valid entries above.
};

struct image_resize_fast_pixel_t {
    f32 channels[4u]{ 0.0f, 0.0f, 0.0f, 1.0f }; // Premultiplied linear RGBA accumulator.
};

image_resize_fast_axis_sample_t ImageResize_CalculateDoubleAxis(
    u32 iDestination,
    u32 cDestination,
    u32 cSource ) noexcept;

bool_t ImageResize_ExtentsEqual(
    const image_extent_t &left,
    const image_extent_t &right ) noexcept
{
    return left.nWidth == right.nWidth &&
           left.nHeight == right.nHeight &&
           left.nDepth == right.nDepth;
}

byte *ImageResize_GetRowUnchecked(
    const image_view_t &view,
    u32 iRow,
    u32 iSlice ) noexcept
{
    return view.pixels.pData +
           static_cast<usize>( iSlice ) * view.cbSlicePitch +
           static_cast<usize>( iRow ) * view.cbRowPitch;
}

const byte *ImageResize_GetRowUnchecked(
    const const_image_view_t &view,
    u32 iRow,
    u32 iSlice ) noexcept
{
    return view.pixels.pData +
           static_cast<usize>( iSlice ) * view.cbSlicePitch +
           static_cast<usize>( iRow ) * view.cbRowPitch;
}

image_resize_axis_sample_t ImageResize_CalculateLinearAxis(
    u32 iDestination,
    u32 cDestination,
    u32 cSource ) noexcept
{
    image_resize_axis_sample_t sample{};
    if ( cSource == 1u ) {
        return sample;
    }

    // Pixel centers map through normalized image space. Subtracting one half
    // converts the mapped center back into source texel-index coordinates.
    const f64 sourcePosition =
        ( ( static_cast<f64>( iDestination ) + 0.5 ) *
          static_cast<f64>( cSource ) /
          static_cast<f64>( cDestination ) ) - 0.5;
    if ( sourcePosition <= 0.0 ) {
        return sample;
    }

    const u32 iLastSource = cSource - 1u;
    if ( sourcePosition >= static_cast<f64>( iLastSource ) ) {
        sample.indices[0] = iLastSource;
        return sample;
    }

    sample.indices[0] = static_cast<u32>( std::floor( sourcePosition ) );
    sample.indices[1] = sample.indices[0] + 1u;
    sample.weights[1] = sourcePosition -
                        static_cast<f64>( sample.indices[0] );
    sample.weights[0] = 1.0 - sample.weights[1];
    sample.cSamples = sample.weights[1] > 0.0 ? 2u : 1u;
    return sample;
}

u32 ImageResize_CalculateNearestIndex(
    u32 iDestination,
    u32 cDestination,
    u32 cSource ) noexcept
{
    // Nearest uses the same center convention as linear filtering but chooses
    // the containing source texel instead of constructing a two-sample pair.
    const f64 sourcePosition =
        ( static_cast<f64>( iDestination ) + 0.5 ) *
        static_cast<f64>( cSource ) /
        static_cast<f64>( cDestination );
    const u32 iSource = static_cast<u32>( std::floor( sourcePosition ) );
    return iSource < cSource ? iSource : cSource - 1u;
}

image_resize_work_pixel_t ImageResize_LoadWorkPixel(
    const byte *pPixel,
    const image_format_info_t &formatInfo,
    image_alpha_mode_t alphaMode ) noexcept
{
    image_resize_work_pixel_t pixel{};
    for ( u8 iChannel = 0u;
          iChannel < formatInfo.cChannels;
          ++iChannel ) {
        f32 value = 0.0f;
        std::memcpy(
            &value,
            pPixel + static_cast<usize>( iChannel ) * sizeof( f32 ),
            sizeof( value ) );
        pixel.channels[iChannel] = value;
    }

    // Filter straight-alpha colors in premultiplied form. Otherwise transparent
    // texels contribute hidden RGB and create dark or colored fringes.
    if ( !formatInfo.bHasAlpha || alphaMode == image_alpha_mode_t::NONE ) {
        pixel.channels[3] = 1.0;
    } else if ( alphaMode == image_alpha_mode_t::STRAIGHT ) {
        pixel.channels[0] *= pixel.channels[3];
        pixel.channels[1] *= pixel.channels[3];
        pixel.channels[2] *= pixel.channels[3];
    }
    return pixel;
}

void ImageResize_StoreWorkPixel(
    byte *pPixel,
    image_resize_work_pixel_t pixel,
    const image_format_info_t &formatInfo,
    image_alpha_mode_t alphaMode ) noexcept
{
    // Restore the descriptor's requested alpha representation only after all
    // weighted samples have been accumulated.
    if ( !formatInfo.bHasAlpha || alphaMode == image_alpha_mode_t::NONE ) {
        pixel.channels[3] = 1.0;
    } else if ( alphaMode == image_alpha_mode_t::STRAIGHT ) {
        if ( pixel.channels[3] == 0.0 ) {
            pixel.channels[0] = 0.0;
            pixel.channels[1] = 0.0;
            pixel.channels[2] = 0.0;
        } else {
            const f64 inverseAlpha = 1.0 / pixel.channels[3];
            pixel.channels[0] *= inverseAlpha;
            pixel.channels[1] *= inverseAlpha;
            pixel.channels[2] *= inverseAlpha;
        }
    }

    for ( u8 iChannel = 0u;
          iChannel < formatInfo.cChannels;
          ++iChannel ) {
        const f32 value = static_cast<f32>( pixel.channels[iChannel] );
        std::memcpy(
            pPixel + static_cast<usize>( iChannel ) * sizeof( f32 ),
            &value,
            sizeof( value ) );
    }
}

const byte *ImageResize_GetPixelUnchecked(
    const const_image_view_t &view,
    u32 iColumn,
    u32 iRow,
    u32 iSlice,
    usize cbPixel ) noexcept
{
    return ImageResize_GetRowUnchecked( view, iRow, iSlice ) +
           static_cast<usize>( iColumn ) * cbPixel;
}

bool_t ImageResize_IsIdentityOrHalfAxis(
    u32 cSource,
    u32 cDestination ) noexcept
{
    return cSource == cDestination ||
           ( ( cSource & 1u ) == 0u &&
             cSource / 2u == cDestination );
}

bool_t ImageResize_IsExactHalfResize(
    const image_extent_t &source,
    const image_extent_t &destination ) noexcept
{
    // Axes left unchanged are allowed so 1D and 2D images share the 3D kernel.
    return ImageResize_IsIdentityOrHalfAxis(
               source.nWidth, destination.nWidth ) &&
           ImageResize_IsIdentityOrHalfAxis(
               source.nHeight, destination.nHeight ) &&
           ImageResize_IsIdentityOrHalfAxis(
               source.nDepth, destination.nDepth );
}

bool_t ImageResize_IsIdentityOrDoubleAxis(
    u32 cSource,
    u32 cDestination ) noexcept
{
    return cSource == cDestination ||
           ( ( cDestination & 1u ) == 0u &&
             cDestination / 2u == cSource );
}

bool_t ImageResize_IsExactDoubleResize(
    const image_extent_t &source,
    const image_extent_t &destination ) noexcept
{
    // As above, unchanged axes permit line, plane, and volume specialization.
    return ImageResize_IsIdentityOrDoubleAxis(
               source.nWidth, destination.nWidth ) &&
           ImageResize_IsIdentityOrDoubleAxis(
               source.nHeight, destination.nHeight ) &&
           ImageResize_IsIdentityOrDoubleAxis(
               source.nDepth, destination.nDepth );
}

image_resize_fast_pixel_t ImageResize_LoadFastPixel(
    const byte *pPixel,
    const image_format_info_t &formatInfo,
    image_alpha_mode_t alphaMode ) noexcept
{
    image_resize_fast_pixel_t pixel{};
    for ( u8 iChannel = 0u;
          iChannel < formatInfo.cChannels;
          ++iChannel ) {
        std::memcpy(
            &pixel.channels[iChannel],
            pPixel + static_cast<usize>( iChannel ) * sizeof( f32 ),
            sizeof( f32 ) );
    }

    if ( !formatInfo.bHasAlpha || alphaMode == image_alpha_mode_t::NONE ) {
        pixel.channels[3] = 1.0f;
    } else if ( alphaMode == image_alpha_mode_t::STRAIGHT ) {
        pixel.channels[0] *= pixel.channels[3];
        pixel.channels[1] *= pixel.channels[3];
        pixel.channels[2] *= pixel.channels[3];
    }
    return pixel;
}

void ImageResize_StoreFastPixel(
    byte *pPixel,
    image_resize_fast_pixel_t pixel,
    const image_format_info_t &formatInfo,
    image_alpha_mode_t alphaMode ) noexcept
{
    if ( !formatInfo.bHasAlpha || alphaMode == image_alpha_mode_t::NONE ) {
        pixel.channels[3] = 1.0f;
    } else if ( alphaMode == image_alpha_mode_t::STRAIGHT ) {
        if ( pixel.channels[3] == 0.0f ) {
            pixel.channels[0] = 0.0f;
            pixel.channels[1] = 0.0f;
            pixel.channels[2] = 0.0f;
        } else {
            const f32 inverseAlpha = 1.0f / pixel.channels[3];
            pixel.channels[0] *= inverseAlpha;
            pixel.channels[1] *= inverseAlpha;
            pixel.channels[2] *= inverseAlpha;
        }
    }

    for ( u8 iChannel = 0u;
          iChannel < formatInfo.cChannels;
          ++iChannel ) {
        std::memcpy(
            pPixel + static_cast<usize>( iChannel ) * sizeof( f32 ),
            &pixel.channels[iChannel],
            sizeof( f32 ) );
    }
}

void ImageResize_AccumulateFastPixel(
    image_resize_fast_pixel_t &result,
    const byte *pSourcePixel,
    const image_format_info_t &formatInfo,
    image_alpha_mode_t alphaMode,
    f32 weight ) noexcept
{
    const image_resize_fast_pixel_t sourcePixel =
        ImageResize_LoadFastPixel(
            pSourcePixel,
            formatInfo,
            alphaMode );
    for ( usize iChannel = 0u; iChannel < 4u; ++iChannel ) {
        result.channels[iChannel] +=
            sourcePixel.channels[iChannel] * weight;
    }
}

bool_t ImageResize_IsRgba32ExactHalf2D(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    return source.desc.pixelFormat == image_pixel_format_t::RGBA32_FLOAT &&
           source.desc.extent.nDepth == 1u &&
           destination.desc.extent.nDepth == 1u &&
           ( source.desc.extent.nWidth & 1u ) == 0u &&
           ( source.desc.extent.nHeight & 1u ) == 0u &&
           source.desc.extent.nWidth / 2u ==
               destination.desc.extent.nWidth &&
           source.desc.extent.nHeight / 2u ==
               destination.desc.extent.nHeight;
}

bool_t ImageResize_IsRgba32ExactDouble2D(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    return source.desc.pixelFormat == image_pixel_format_t::RGBA32_FLOAT &&
           source.desc.extent.nDepth == 1u &&
           destination.desc.extent.nDepth == 1u &&
           ( destination.desc.extent.nWidth & 1u ) == 0u &&
           ( destination.desc.extent.nHeight & 1u ) == 0u &&
           destination.desc.extent.nWidth / 2u ==
               source.desc.extent.nWidth &&
           destination.desc.extent.nHeight / 2u ==
               source.desc.extent.nHeight;
}

// Texture mip generation overwhelmingly uses RGBA32 working surfaces and a
// 2x2 footprint. A fixed kernel avoids tiny memcpy calls, format loops, and
// sample dispatch while retaining the exact straight-alpha filtering policy.
void ImageResize_Rgba32ExactHalf2DUnchecked(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    constexpr usize kPixelSize = sizeof( f32 ) * 4u;
    for ( u32 iDestinationRow = 0u;
          iDestinationRow < destination.desc.extent.nHeight;
          ++iDestinationRow ) {
        const byte *pSourceRow0 = ImageResize_GetRowUnchecked(
            source,
            iDestinationRow * 2u,
            0u );
        const byte *pSourceRow1 = ImageResize_GetRowUnchecked(
            source,
            iDestinationRow * 2u + 1u,
            0u );
        byte *pDestinationPixel = ImageResize_GetRowUnchecked(
            destination,
            iDestinationRow,
            0u );

        for ( u32 iDestinationColumn = 0u;
              iDestinationColumn < destination.desc.extent.nWidth;
              ++iDestinationColumn ) {
            const usize iSourceByte =
                static_cast<usize>( iDestinationColumn ) * 2u * kPixelSize;
            f32 sourcePixels[4u][4u]{};
            std::memcpy(
                sourcePixels[0u],
                pSourceRow0 + iSourceByte,
                kPixelSize );
            std::memcpy(
                sourcePixels[1u],
                pSourceRow0 + iSourceByte + kPixelSize,
                kPixelSize );
            std::memcpy(
                sourcePixels[2u],
                pSourceRow1 + iSourceByte,
                kPixelSize );
            std::memcpy(
                sourcePixels[3u],
                pSourceRow1 + iSourceByte + kPixelSize,
                kPixelSize );

            f32 result[4u]{};
            if ( source.desc.alphaMode == image_alpha_mode_t::STRAIGHT ) {
                const f32 alphaSum =
                    sourcePixels[0u][3u] + sourcePixels[1u][3u] +
                    sourcePixels[2u][3u] + sourcePixels[3u][3u];
                result[3u] = alphaSum * 0.25f;
                if ( alphaSum != 0.0f ) {
                    for ( usize iChannel = 0u;
                          iChannel < 3u;
                          ++iChannel ) {
                        result[iChannel] =
                            ( sourcePixels[0u][iChannel] *
                                  sourcePixels[0u][3u] +
                              sourcePixels[1u][iChannel] *
                                  sourcePixels[1u][3u] +
                              sourcePixels[2u][iChannel] *
                                  sourcePixels[2u][3u] +
                              sourcePixels[3u][iChannel] *
                                  sourcePixels[3u][3u] ) /
                            alphaSum;
                    }
                }
            } else {
                for ( usize iChannel = 0u;
                      iChannel < 4u;
                      ++iChannel ) {
                    result[iChannel] =
                        ( sourcePixels[0u][iChannel] +
                          sourcePixels[1u][iChannel] +
                          sourcePixels[2u][iChannel] +
                          sourcePixels[3u][iChannel] ) *
                        0.25f;
                }
                if ( source.desc.alphaMode == image_alpha_mode_t::NONE ) {
                    result[3u] = 1.0f;
                }
            }

            std::memcpy( pDestinationPixel, result, kPixelSize );
            pDestinationPixel += kPixelSize;
        }
    }
}

void ImageResize_Rgba32ExactDouble2DUnchecked(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    // This is the common 2x authoring preview path. Four bilinear samples are
    // loaded directly as RGBA32 and combined without generic format dispatch.
    constexpr usize kPixelSize = sizeof( f32 ) * 4u;
    for ( u32 iDestinationRow = 0u;
          iDestinationRow < destination.desc.extent.nHeight;
          ++iDestinationRow ) {
        const image_resize_fast_axis_sample_t ySample =
            ImageResize_CalculateDoubleAxis(
                iDestinationRow,
                destination.desc.extent.nHeight,
                source.desc.extent.nHeight );
        const byte *pSourceRow0 = ImageResize_GetRowUnchecked(
            source,
            ySample.indices[0u],
            0u );
        const byte *pSourceRow1 = ImageResize_GetRowUnchecked(
            source,
            ySample.indices[ySample.cSamples - 1u],
            0u );
        const f32 yWeight1 = ySample.cSamples == 2u
            ? ySample.weights[1u]
            : 0.0f;
        const f32 yWeight0 = 1.0f - yWeight1;
        byte *pDestinationPixel = ImageResize_GetRowUnchecked(
            destination,
            iDestinationRow,
            0u );

        for ( u32 iDestinationColumn = 0u;
              iDestinationColumn < destination.desc.extent.nWidth;
              ++iDestinationColumn ) {
            const image_resize_fast_axis_sample_t xSample =
                ImageResize_CalculateDoubleAxis(
                    iDestinationColumn,
                    destination.desc.extent.nWidth,
                    source.desc.extent.nWidth );
            const u32 iSourceColumn1 =
                xSample.indices[xSample.cSamples - 1u];
            const f32 xWeight1 = xSample.cSamples == 2u
                ? xSample.weights[1u]
                : 0.0f;
            const f32 xWeight0 = 1.0f - xWeight1;
            const f32 sampleWeights[4u]{
                xWeight0 * yWeight0,
                xWeight1 * yWeight0,
                xWeight0 * yWeight1,
                xWeight1 * yWeight1
            };

            f32 sourcePixels[4u][4u]{};
            std::memcpy(
                sourcePixels[0u],
                pSourceRow0 +
                    static_cast<usize>( xSample.indices[0u] ) * kPixelSize,
                kPixelSize );
            std::memcpy(
                sourcePixels[1u],
                pSourceRow0 +
                    static_cast<usize>( iSourceColumn1 ) * kPixelSize,
                kPixelSize );
            std::memcpy(
                sourcePixels[2u],
                pSourceRow1 +
                    static_cast<usize>( xSample.indices[0u] ) * kPixelSize,
                kPixelSize );
            std::memcpy(
                sourcePixels[3u],
                pSourceRow1 +
                    static_cast<usize>( iSourceColumn1 ) * kPixelSize,
                kPixelSize );

            f32 result[4u]{};
            if ( source.desc.alphaMode == image_alpha_mode_t::STRAIGHT ) {
                for ( usize iSample = 0u; iSample < 4u; ++iSample ) {
                    const f32 weightedAlpha =
                        sourcePixels[iSample][3u] * sampleWeights[iSample];
                    result[3u] += weightedAlpha;
                    result[0u] += sourcePixels[iSample][0u] * weightedAlpha;
                    result[1u] += sourcePixels[iSample][1u] * weightedAlpha;
                    result[2u] += sourcePixels[iSample][2u] * weightedAlpha;
                }
                if ( result[3u] != 0.0f ) {
                    const f32 inverseAlpha = 1.0f / result[3u];
                    result[0u] *= inverseAlpha;
                    result[1u] *= inverseAlpha;
                    result[2u] *= inverseAlpha;
                } else {
                    result[0u] = 0.0f;
                    result[1u] = 0.0f;
                    result[2u] = 0.0f;
                }
            } else {
                for ( usize iSample = 0u; iSample < 4u; ++iSample ) {
                    for ( usize iChannel = 0u;
                          iChannel < 4u;
                          ++iChannel ) {
                        result[iChannel] +=
                            sourcePixels[iSample][iChannel] *
                            sampleWeights[iSample];
                    }
                }
                if ( source.desc.alphaMode == image_alpha_mode_t::NONE ) {
                    result[3u] = 1.0f;
                }
            }

            std::memcpy( pDestinationPixel, result, kPixelSize );
            pDestinationPixel += kPixelSize;
        }
    }
}

// A 2:1 pixel-center linear sample and a 2:1 box footprint both reduce to
// averaging the same 2x2 (or 2x2x2) source neighborhood. Keeping that common
// cooker path free of per-pixel floor/ceil and weight calculations matters far
// more than specializing uncommon arbitrary resize ratios.
void ImageResize_FloatExactHalfUnchecked(
    const image_view_t &destination,
    const const_image_view_t &source,
    const image_format_info_t &formatInfo ) noexcept
{
    const u32 cSamplesX = source.desc.extent.nWidth ==
                                  destination.desc.extent.nWidth
        ? 1u
        : 2u;
    const u32 cSamplesY = source.desc.extent.nHeight ==
                                  destination.desc.extent.nHeight
        ? 1u
        : 2u;
    const u32 cSamplesZ = source.desc.extent.nDepth ==
                                  destination.desc.extent.nDepth
        ? 1u
        : 2u;
    const f32 sampleWeight = 1.0f / static_cast<f32>(
        cSamplesX * cSamplesY * cSamplesZ );

    for ( u32 iDestinationSlice = 0u;
          iDestinationSlice < destination.desc.extent.nDepth;
          ++iDestinationSlice ) {
        const u32 iSourceSlice = iDestinationSlice * cSamplesZ;
        for ( u32 iDestinationRow = 0u;
              iDestinationRow < destination.desc.extent.nHeight;
              ++iDestinationRow ) {
            const u32 iSourceRow = iDestinationRow * cSamplesY;
            byte *pDestinationPixel = ImageResize_GetRowUnchecked(
                destination,
                iDestinationRow,
                iDestinationSlice );

            for ( u32 iDestinationColumn = 0u;
                  iDestinationColumn < destination.desc.extent.nWidth;
                  ++iDestinationColumn ) {
                const u32 iSourceColumn =
                    iDestinationColumn * cSamplesX;
                image_resize_fast_pixel_t result{};
                result.channels[3] = 0.0f;

                for ( u32 iZ = 0u; iZ < cSamplesZ; ++iZ ) {
                    for ( u32 iY = 0u; iY < cSamplesY; ++iY ) {
                        for ( u32 iX = 0u; iX < cSamplesX; ++iX ) {
                            ImageResize_AccumulateFastPixel(
                                result,
                                ImageResize_GetPixelUnchecked(
                                    source,
                                    iSourceColumn + iX,
                                    iSourceRow + iY,
                                    iSourceSlice + iZ,
                                    formatInfo.cbPixel ),
                                formatInfo,
                                source.desc.alphaMode,
                                sampleWeight );
                        }
                    }
                }

                ImageResize_StoreFastPixel(
                    pDestinationPixel,
                    result,
                    formatInfo,
                    destination.desc.alphaMode );
                pDestinationPixel += formatInfo.cbPixel;
            }
        }
    }
}

image_resize_fast_axis_sample_t ImageResize_CalculateDoubleAxis(
    u32 iDestination,
    u32 cDestination,
    u32 cSource ) noexcept
{
    // Keep the general pixel-center mapping for boundary behavior while using
    // float weights in the specialized exact-double kernels.
    image_resize_fast_axis_sample_t sample{};
    if ( cDestination == cSource ) {
        sample.indices[0] = iDestination;
        return sample;
    }

    if ( iDestination == 0u ) {
        return sample;
    }
    if ( iDestination == cDestination - 1u ) {
        sample.indices[0] = cSource - 1u;
        return sample;
    }

    sample.cSamples = 2u;
    if ( ( iDestination & 1u ) != 0u ) {
        sample.indices[0] = iDestination / 2u;
        sample.indices[1] = sample.indices[0] + 1u;
        sample.weights[0] = 0.75f;
        sample.weights[1] = 0.25f;
    } else {
        sample.indices[1] = iDestination / 2u;
        sample.indices[0] = sample.indices[1] - 1u;
        sample.weights[0] = 0.25f;
        sample.weights[1] = 0.75f;
    }
    return sample;
}

void ImageResize_FloatExactDoubleUnchecked(
    const image_view_t &destination,
    const const_image_view_t &source,
    const image_format_info_t &formatInfo ) noexcept
{
    for ( u32 iDestinationSlice = 0u;
          iDestinationSlice < destination.desc.extent.nDepth;
          ++iDestinationSlice ) {
        const image_resize_fast_axis_sample_t zSample =
            ImageResize_CalculateDoubleAxis(
                iDestinationSlice,
                destination.desc.extent.nDepth,
                source.desc.extent.nDepth );
        for ( u32 iDestinationRow = 0u;
              iDestinationRow < destination.desc.extent.nHeight;
              ++iDestinationRow ) {
            const image_resize_fast_axis_sample_t ySample =
                ImageResize_CalculateDoubleAxis(
                    iDestinationRow,
                    destination.desc.extent.nHeight,
                    source.desc.extent.nHeight );
            byte *pDestinationPixel = ImageResize_GetRowUnchecked(
                destination,
                iDestinationRow,
                iDestinationSlice );

            for ( u32 iDestinationColumn = 0u;
                  iDestinationColumn < destination.desc.extent.nWidth;
                  ++iDestinationColumn ) {
                const image_resize_fast_axis_sample_t xSample =
                    ImageResize_CalculateDoubleAxis(
                        iDestinationColumn,
                        destination.desc.extent.nWidth,
                        source.desc.extent.nWidth );
                image_resize_fast_pixel_t result{};
                result.channels[3] = 0.0f;

                for ( u8 iZ = 0u; iZ < zSample.cSamples; ++iZ ) {
                    for ( u8 iY = 0u; iY < ySample.cSamples; ++iY ) {
                        for ( u8 iX = 0u; iX < xSample.cSamples; ++iX ) {
                            ImageResize_AccumulateFastPixel(
                                result,
                                ImageResize_GetPixelUnchecked(
                                    source,
                                    xSample.indices[iX],
                                    ySample.indices[iY],
                                    zSample.indices[iZ],
                                    formatInfo.cbPixel ),
                                formatInfo,
                                source.desc.alphaMode,
                                xSample.weights[iX] *
                                    ySample.weights[iY] *
                                    zSample.weights[iZ] );
                        }
                    }
                }

                ImageResize_StoreFastPixel(
                    pDestinationPixel,
                    result,
                    formatInfo,
                    destination.desc.alphaMode );
                pDestinationPixel += formatInfo.cbPixel;
            }
        }
    }
}

void ImageResize_NearestExactHalfUnchecked(
    const image_view_t &destination,
    const const_image_view_t &source,
    usize cbPixel ) noexcept
{
    // Exact half nearest sampling always selects the second texel in each 2-wide
    // footprint under the shared pixel-center convention.
    const u32 nStepX = source.desc.extent.nWidth ==
                               destination.desc.extent.nWidth
        ? 1u
        : 2u;
    const u32 nStepY = source.desc.extent.nHeight ==
                               destination.desc.extent.nHeight
        ? 1u
        : 2u;
    const u32 nStepZ = source.desc.extent.nDepth ==
                               destination.desc.extent.nDepth
        ? 1u
        : 2u;

    for ( u32 iDestinationSlice = 0u;
          iDestinationSlice < destination.desc.extent.nDepth;
          ++iDestinationSlice ) {
        const u32 iSourceSlice =
            iDestinationSlice * nStepZ + ( nStepZ - 1u );
        for ( u32 iDestinationRow = 0u;
              iDestinationRow < destination.desc.extent.nHeight;
              ++iDestinationRow ) {
            const u32 iSourceRow =
                iDestinationRow * nStepY + ( nStepY - 1u );
            const byte *pSourceRow = ImageResize_GetRowUnchecked(
                source,
                iSourceRow,
                iSourceSlice );
            byte *pDestinationPixel = ImageResize_GetRowUnchecked(
                destination,
                iDestinationRow,
                iDestinationSlice );

            for ( u32 iDestinationColumn = 0u;
                  iDestinationColumn < destination.desc.extent.nWidth;
                  ++iDestinationColumn ) {
                const u32 iSourceColumn =
                    iDestinationColumn * nStepX + ( nStepX - 1u );
                std::memcpy(
                    pDestinationPixel,
                    pSourceRow +
                        static_cast<usize>( iSourceColumn ) * cbPixel,
                    cbPixel );
                pDestinationPixel += cbPixel;
            }
        }
    }
}

void ImageResize_NearestUnchecked(
    const image_view_t &destination,
    const const_image_view_t &source,
    usize cbPixel ) noexcept
{
    // Nearest copies raw pixel bytes and therefore preserves integer formats,
    // color encodings, and payload channels without interpretation.
    for ( u32 iDestinationSlice = 0u;
          iDestinationSlice < destination.desc.extent.nDepth;
          ++iDestinationSlice ) {
        const u32 iSourceSlice = ImageResize_CalculateNearestIndex(
            iDestinationSlice,
            destination.desc.extent.nDepth,
            source.desc.extent.nDepth );
        for ( u32 iDestinationRow = 0u;
              iDestinationRow < destination.desc.extent.nHeight;
              ++iDestinationRow ) {
            const u32 iSourceRow = ImageResize_CalculateNearestIndex(
                iDestinationRow,
                destination.desc.extent.nHeight,
                source.desc.extent.nHeight );
            const byte *pSourceRow = ImageResize_GetRowUnchecked(
                source,
                iSourceRow,
                iSourceSlice );
            byte *pDestinationPixel = ImageResize_GetRowUnchecked(
                destination,
                iDestinationRow,
                iDestinationSlice );

            for ( u32 iDestinationColumn = 0u;
                  iDestinationColumn < destination.desc.extent.nWidth;
                  ++iDestinationColumn ) {
                const u32 iSourceColumn = ImageResize_CalculateNearestIndex(
                    iDestinationColumn,
                    destination.desc.extent.nWidth,
                    source.desc.extent.nWidth );
                std::memcpy(
                    pDestinationPixel,
                    pSourceRow + static_cast<usize>( iSourceColumn ) * cbPixel,
                    cbPixel );
                pDestinationPixel += cbPixel;
            }
        }
    }
}

void ImageResize_LinearUnchecked(
    const image_view_t &destination,
    const const_image_view_t &source,
    const image_format_info_t &formatInfo ) noexcept
{
    // General trilinear filtering evaluates at most eight source samples. This
    // path favors a clear reference implementation over ratio-specific tricks.
    const usize cbPixel = formatInfo.cbPixel;
    for ( u32 iDestinationSlice = 0u;
          iDestinationSlice < destination.desc.extent.nDepth;
          ++iDestinationSlice ) {
        const image_resize_axis_sample_t zSample =
            ImageResize_CalculateLinearAxis(
                iDestinationSlice,
                destination.desc.extent.nDepth,
                source.desc.extent.nDepth );
        for ( u32 iDestinationRow = 0u;
              iDestinationRow < destination.desc.extent.nHeight;
              ++iDestinationRow ) {
            const image_resize_axis_sample_t ySample =
                ImageResize_CalculateLinearAxis(
                    iDestinationRow,
                    destination.desc.extent.nHeight,
                    source.desc.extent.nHeight );
            byte *pDestinationPixel = ImageResize_GetRowUnchecked(
                destination,
                iDestinationRow,
                iDestinationSlice );

            for ( u32 iDestinationColumn = 0u;
                  iDestinationColumn < destination.desc.extent.nWidth;
                  ++iDestinationColumn ) {
                const image_resize_axis_sample_t xSample =
                    ImageResize_CalculateLinearAxis(
                        iDestinationColumn,
                        destination.desc.extent.nWidth,
                        source.desc.extent.nWidth );
                image_resize_work_pixel_t result{};
                result.channels[0] = 0.0;
                result.channels[1] = 0.0;
                result.channels[2] = 0.0;
                result.channels[3] = 0.0;

                for ( u8 iZ = 0u; iZ < zSample.cSamples; ++iZ ) {
                    for ( u8 iY = 0u; iY < ySample.cSamples; ++iY ) {
                        for ( u8 iX = 0u; iX < xSample.cSamples; ++iX ) {
                            const f64 weight =
                                zSample.weights[iZ] *
                                ySample.weights[iY] *
                                xSample.weights[iX];
                            const image_resize_work_pixel_t sourcePixel =
                                ImageResize_LoadWorkPixel(
                                    ImageResize_GetPixelUnchecked(
                                        source,
                                        xSample.indices[iX],
                                        ySample.indices[iY],
                                        zSample.indices[iZ],
                                        cbPixel ),
                                    formatInfo,
                                    source.desc.alphaMode );
                            for ( usize iChannel = 0u;
                                  iChannel < 4u;
                                  ++iChannel ) {
                                result.channels[iChannel] +=
                                    sourcePixel.channels[iChannel] * weight;
                            }
                        }
                    }
                }

                ImageResize_StoreWorkPixel(
                    pDestinationPixel,
                    result,
                    formatInfo,
                    destination.desc.alphaMode );
                pDestinationPixel += cbPixel;
            }
        }
    }
}

f64 ImageResize_BoxAxisStart(
    u32 iDestination,
    u32 cDestination,
    u32 cSource ) noexcept
{
    return static_cast<f64>( iDestination ) *
           static_cast<f64>( cSource ) /
           static_cast<f64>( cDestination );
}

f64 ImageResize_BoxAxisEnd(
    u32 iDestination,
    u32 cDestination,
    u32 cSource ) noexcept
{
    return static_cast<f64>( iDestination + 1u ) *
           static_cast<f64>( cSource ) /
           static_cast<f64>( cDestination );
}

f64 ImageResize_BoxWeight(
    f64 start,
    f64 end,
    u32 iSource ) noexcept
{
    // Source texels are unit intervals. Their overlap with the destination
    // footprint is the unnormalized area contribution on this axis.
    const f64 pixelStart = static_cast<f64>( iSource );
    const f64 pixelEnd = pixelStart + 1.0;
    const f64 overlapStart = start > pixelStart ? start : pixelStart;
    const f64 overlapEnd = end < pixelEnd ? end : pixelEnd;
    return overlapEnd > overlapStart ? overlapEnd - overlapStart : 0.0;
}

u32 ImageResize_BoxLastIndex( f64 end, u32 cSource ) noexcept
{
    const f64 lastPosition = std::ceil( end ) - 1.0;
    const u32 iLast = lastPosition > 0.0
        ? static_cast<u32>( lastPosition )
        : 0u;
    return iLast < cSource ? iLast : cSource - 1u;
}

void ImageResize_BoxUnchecked(
    const image_view_t &destination,
    const const_image_view_t &source,
    const image_format_info_t &formatInfo ) noexcept
{
    // Integrate every source texel touched by the destination footprint. Axis
    // overlap products form the volume weight for 1D, 2D, and 3D images.
    const usize cbPixel = formatInfo.cbPixel;
    for ( u32 iDestinationSlice = 0u;
          iDestinationSlice < destination.desc.extent.nDepth;
          ++iDestinationSlice ) {
        const f64 zStart = ImageResize_BoxAxisStart(
            iDestinationSlice,
            destination.desc.extent.nDepth,
            source.desc.extent.nDepth );
        const f64 zEnd = ImageResize_BoxAxisEnd(
            iDestinationSlice,
            destination.desc.extent.nDepth,
            source.desc.extent.nDepth );
        const u32 iFirstZ = static_cast<u32>( std::floor( zStart ) );
        const u32 iLastZ = ImageResize_BoxLastIndex(
            zEnd,
            source.desc.extent.nDepth );

        for ( u32 iDestinationRow = 0u;
              iDestinationRow < destination.desc.extent.nHeight;
              ++iDestinationRow ) {
            const f64 yStart = ImageResize_BoxAxisStart(
                iDestinationRow,
                destination.desc.extent.nHeight,
                source.desc.extent.nHeight );
            const f64 yEnd = ImageResize_BoxAxisEnd(
                iDestinationRow,
                destination.desc.extent.nHeight,
                source.desc.extent.nHeight );
            const u32 iFirstY = static_cast<u32>( std::floor( yStart ) );
            const u32 iLastY = ImageResize_BoxLastIndex(
                yEnd,
                source.desc.extent.nHeight );
            byte *pDestinationPixel = ImageResize_GetRowUnchecked(
                destination,
                iDestinationRow,
                iDestinationSlice );

            for ( u32 iDestinationColumn = 0u;
                  iDestinationColumn < destination.desc.extent.nWidth;
                  ++iDestinationColumn ) {
                const f64 xStart = ImageResize_BoxAxisStart(
                    iDestinationColumn,
                    destination.desc.extent.nWidth,
                    source.desc.extent.nWidth );
                const f64 xEnd = ImageResize_BoxAxisEnd(
                    iDestinationColumn,
                    destination.desc.extent.nWidth,
                    source.desc.extent.nWidth );
                const u32 iFirstX = static_cast<u32>( std::floor( xStart ) );
                const u32 iLastX = ImageResize_BoxLastIndex(
                    xEnd,
                    source.desc.extent.nWidth );
                image_resize_work_pixel_t result{};
                result.channels[0] = 0.0;
                result.channels[1] = 0.0;
                result.channels[2] = 0.0;
                result.channels[3] = 0.0;
                f64 totalWeight = 0.0;

                for ( u32 iZ = iFirstZ; ; ) {
                    const f64 zWeight = ImageResize_BoxWeight(
                        zStart, zEnd, iZ );
                    for ( u32 iY = iFirstY; ; ) {
                        const f64 yWeight = ImageResize_BoxWeight(
                            yStart, yEnd, iY );
                        for ( u32 iX = iFirstX; ; ) {
                            const f64 weight = zWeight * yWeight *
                                               ImageResize_BoxWeight(
                                                   xStart, xEnd, iX );
                            const image_resize_work_pixel_t sourcePixel =
                                ImageResize_LoadWorkPixel(
                                    ImageResize_GetPixelUnchecked(
                                        source,
                                        iX,
                                        iY,
                                        iZ,
                                        cbPixel ),
                                    formatInfo,
                                    source.desc.alphaMode );
                            for ( usize iChannel = 0u;
                                  iChannel < 4u;
                                  ++iChannel ) {
                                result.channels[iChannel] +=
                                    sourcePixel.channels[iChannel] * weight;
                            }
                            totalWeight += weight;

                            if ( iX == iLastX ) {
                                break;
                            }
                            ++iX;
                        }
                        if ( iY == iLastY ) {
                            break;
                        }
                        ++iY;
                    }
                    if ( iZ == iLastZ ) {
                        break;
                    }
                    ++iZ;
                }

                if ( totalWeight > 0.0 ) {
                    const f64 inverseWeight = 1.0 / totalWeight;
                    for ( usize iChannel = 0u;
                          iChannel < 4u;
                          ++iChannel ) {
                        result.channels[iChannel] *= inverseWeight;
                    }
                }
                ImageResize_StoreWorkPixel(
                    pDestinationPixel,
                    result,
                    formatInfo,
                    destination.desc.alphaMode );
                pDestinationPixel += cbPixel;
            }
        }
    }
}

image_resize_status_t ImageResize_MapCopyStatus(
    image_process_status_t status ) noexcept
{
    return status == image_process_status_t::OK
        ? image_resize_status_t::OK
        : image_resize_status_t::OVERLAPPING_MEMORY;
}

} // namespace

bool_t ImageResize_IsFilterSupported(
    const image_desc_t &desc,
    image_resize_filter_t filter ) noexcept
{
    if ( ImageFormat_ValidateDesc( desc ) != image_format_status_t::OK ) {
        return CY_FALSE;
    }
    if ( filter == image_resize_filter_t::NEAREST ) {
        // Raw selection does not perform arithmetic on channel values.
        return CY_TRUE;
    }
    if ( filter != image_resize_filter_t::LINEAR &&
         filter != image_resize_filter_t::BOX ) {
        return CY_FALSE;
    }

    // Arithmetic filters deliberately accept only linear float working images.
    // Callers convert packed or sRGB source art before resizing it.
    const image_format_info_t *pInfo =
        ImageFormat_GetInfo( desc.pixelFormat );
    return pInfo->numericType == image_numeric_type_t::FLOAT &&
           pInfo->cbComponent == sizeof( f32 ) &&
           desc.colorSpace == image_color_space_t::LINEAR;
}

image_resize_status_t ImageResize(
    const image_view_t &destination,
    const const_image_view_t &source,
    image_resize_filter_t filter ) noexcept
{
    if ( ImageView_Validate( destination ) != image_view_status_t::OK ) {
        return image_resize_status_t::INVALID_DESTINATION_VIEW;
    }
    if ( ImageView_Validate( source ) != image_view_status_t::OK ) {
        return image_resize_status_t::INVALID_SOURCE_VIEW;
    }
    if ( filter != image_resize_filter_t::NEAREST &&
         filter != image_resize_filter_t::LINEAR &&
         filter != image_resize_filter_t::BOX ) {
        return image_resize_status_t::INVALID_FILTER;
    }
    if ( destination.desc.pixelFormat != source.desc.pixelFormat ) {
        return image_resize_status_t::PIXEL_FORMAT_MISMATCH;
    }
    if ( destination.desc.colorSpace != source.desc.colorSpace ) {
        return image_resize_status_t::COLOR_SPACE_MISMATCH;
    }
    if ( destination.desc.alphaMode != source.desc.alphaMode ) {
        return image_resize_status_t::ALPHA_MODE_MISMATCH;
    }
    if ( !ImageResize_IsFilterSupported( source.desc, filter ) ||
         !ImageResize_IsFilterSupported( destination.desc, filter ) ) {
        return image_resize_status_t::FILTER_FORMAT_NOT_SUPPORTED;
    }
    if ( filter == image_resize_filter_t::BOX &&
         ( destination.desc.extent.nWidth > source.desc.extent.nWidth ||
           destination.desc.extent.nHeight > source.desc.extent.nHeight ||
           destination.desc.extent.nDepth > source.desc.extent.nDepth ) ) {
        return image_resize_status_t::BOX_REQUIRES_DOWNSAMPLING;
    }

    if ( ImageResize_ExtentsEqual(
             destination.desc.extent,
             source.desc.extent ) ) {
        // Preserve row padding policy by delegating the identity case to the
        // logical-pixel copy operation.
        return ImageResize_MapCopyStatus(
            ImageProcess_Copy( destination, source ) );
    }

    if ( Cy_MemRangesOverlap(
             destination.pixels.pData,
             destination.pixels.nCount,
             source.pixels.pData,
             source.pixels.cbSize ) ) {
        return image_resize_status_t::OVERLAPPING_MEMORY;
    }

    // Dispatch the common exact-ratio cases first, then fall back to the scalar
    // reference kernels for arbitrary dimensions.
    const image_format_info_t *pInfo =
        ImageFormat_GetInfo( source.desc.pixelFormat );
    const bool_t bExactHalf = ImageResize_IsExactHalfResize(
        source.desc.extent,
        destination.desc.extent );
    const bool_t bFloat32 =
        pInfo->numericType == image_numeric_type_t::FLOAT &&
        pInfo->cbComponent == sizeof( f32 );
    switch ( filter ) {
        case image_resize_filter_t::NEAREST:
            if ( bExactHalf ) {
                ImageResize_NearestExactHalfUnchecked(
                    destination,
                    source,
                    pInfo->cbPixel );
            } else {
                ImageResize_NearestUnchecked(
                    destination,
                    source,
                    pInfo->cbPixel );
            }
            break;
        case image_resize_filter_t::LINEAR:
            if ( ImageResize_IsRgba32ExactHalf2D(
                     destination,
                     source ) ) {
                ImageResize_Rgba32ExactHalf2DUnchecked(
                    destination,
                    source );
            } else if ( ImageResize_IsRgba32ExactDouble2D(
                            destination,
                            source ) ) {
                ImageResize_Rgba32ExactDouble2DUnchecked(
                    destination,
                    source );
            } else if ( bFloat32 && bExactHalf ) {
                ImageResize_FloatExactHalfUnchecked(
                    destination,
                    source,
                    *pInfo );
            } else if ( bFloat32 && ImageResize_IsExactDoubleResize(
                                      source.desc.extent,
                                      destination.desc.extent ) ) {
                ImageResize_FloatExactDoubleUnchecked(
                    destination,
                    source,
                    *pInfo );
            } else {
                ImageResize_LinearUnchecked( destination, source, *pInfo );
            }
            break;
        case image_resize_filter_t::BOX:
            if ( ImageResize_IsRgba32ExactHalf2D(
                     destination,
                     source ) ) {
                ImageResize_Rgba32ExactHalf2DUnchecked(
                    destination,
                    source );
            } else if ( bFloat32 && bExactHalf ) {
                ImageResize_FloatExactHalfUnchecked(
                    destination,
                    source,
                    *pInfo );
            } else {
                ImageResize_BoxUnchecked( destination, source, *pInfo );
            }
            break;
        default:
            return image_resize_status_t::INVALID_FILTER;
    }
    return image_resize_status_t::OK;
}

const char *ImageResize_StatusName(
    image_resize_status_t status ) noexcept
{
    switch ( status ) {
        case image_resize_status_t::OK:
            return "OK";
        case image_resize_status_t::INVALID_SOURCE_VIEW:
            return "INVALID_SOURCE_VIEW";
        case image_resize_status_t::INVALID_DESTINATION_VIEW:
            return "INVALID_DESTINATION_VIEW";
        case image_resize_status_t::INVALID_FILTER:
            return "INVALID_FILTER";
        case image_resize_status_t::PIXEL_FORMAT_MISMATCH:
            return "PIXEL_FORMAT_MISMATCH";
        case image_resize_status_t::COLOR_SPACE_MISMATCH:
            return "COLOR_SPACE_MISMATCH";
        case image_resize_status_t::ALPHA_MODE_MISMATCH:
            return "ALPHA_MODE_MISMATCH";
        case image_resize_status_t::FILTER_FORMAT_NOT_SUPPORTED:
            return "FILTER_FORMAT_NOT_SUPPORTED";
        case image_resize_status_t::BOX_REQUIRES_DOWNSAMPLING:
            return "BOX_REQUIRES_DOWNSAMPLING";
        case image_resize_status_t::OVERLAPPING_MEMORY:
            return "OVERLAPPING_MEMORY";
        default:
            return "UNKNOWN_IMAGE_RESIZE_STATUS";
    }
}

} // namespace cypher::common
