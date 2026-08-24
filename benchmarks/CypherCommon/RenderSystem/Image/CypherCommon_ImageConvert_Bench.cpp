//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/RenderSystem/Image/CypherCommon_ImageConvert_Bench.cpp
//  Purpose: Measures image format, color-space, alpha, and swizzle conversion.
//  Details: Allocations and initialization occur outside timed loops so results
//           expose scalar conversion cost from 256-pixel through 4K images.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageConvert.h"
#include "CypherCommon_ImageSurface.h"
#include "CypherCommon_MemoryOps.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

image_desc_t SquareDesc(
    u32 nDimension,
    image_pixel_format_t format,
    image_color_space_t colorSpace,
    image_alpha_mode_t alphaMode ) noexcept
{
    return {
        { nDimension, nDimension, 1u },
        format,
        colorSpace,
        alphaMode
    };
}

bool CreateSurface(
    image_surface_t &surface,
    const image_desc_t &desc ) noexcept
{
    return ImageSurface_Create(
               &surface,
               Allocator_GetSystem(),
               desc,
               image_surface_init_t::UNINITIALIZED,
               64u ) == image_surface_status_t::OK;
}

void SeedSurface( image_surface_t &surface ) noexcept
{
    Cy_MemSet(
        surface.allocation.pData,
        0x3Fu,
        surface.layout.cbTotalSize );
}

void SetConversionWork(
    benchmark::State &state,
    const image_desc_t &source,
    const image_desc_t &destination )
{
    const image_format_info_t *pSourceInfo =
        ImageFormat_GetInfo( source.pixelFormat );
    const image_format_info_t *pDestinationInfo =
        ImageFormat_GetInfo( destination.pixelFormat );
    const int64_t cPixels =
        static_cast<int64_t>( source.extent.nWidth ) *
        static_cast<int64_t>( source.extent.nHeight ) *
        static_cast<int64_t>( source.extent.nDepth );
    const int64_t cbTraffic = cPixels *
        static_cast<int64_t>(
            pSourceInfo->cbPixel + pDestinationInfo->cbPixel );

    state.SetItemsProcessed( state.iterations() * cPixels );
    state.SetBytesProcessed( state.iterations() * cbTraffic );
    state.counters["source_bytes_per_pixel"] = pSourceInfo->cbPixel;
    state.counters["destination_bytes_per_pixel"] = pDestinationInfo->cbPixel;
}

void RunConversion(
    benchmark::State &state,
    const image_desc_t &sourceDesc,
    const image_desc_t &destinationDesc,
    const image_convert_options_t &options = {} )
{
    image_surface_t source{};
    image_surface_t destination{};
    if ( !CreateSurface( source, sourceDesc ) ||
         !CreateSurface( destination, destinationDesc ) ) {
        state.SkipWithError( "Could not allocate conversion surfaces" );
        return;
    }
    SeedSurface( source );

    const const_image_view_t sourceView = ImageSurface_GetView(
        static_cast<const image_surface_t *>( &source ) );
    const image_view_t destinationView = ImageSurface_GetView( &destination );
    for ( auto _ : state ) {
        const image_convert_status_t status = ImageConvert(
            destinationView,
            sourceView,
            options );
        if ( status != image_convert_status_t::OK ) {
            state.SkipWithError( "Image conversion failed during measurement" );
            break;
        }
        benchmark::ClobberMemory();
    }
    SetConversionWork( state, sourceDesc, destinationDesc );
}

void BM_ImageConvert_Rgba8ToRgba16( benchmark::State &state )
{
    const u32 nDimension = static_cast<u32>( state.range( 0 ) );
    RunConversion(
        state,
        SquareDesc(
            nDimension,
            image_pixel_format_t::RGBA8_UNORM,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::STRAIGHT ),
        SquareDesc(
            nDimension,
            image_pixel_format_t::RGBA16_UNORM,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::STRAIGHT ) );
}

void BM_ImageConvert_Rgba8SrgbToRgba32Linear( benchmark::State &state )
{
    const u32 nDimension = static_cast<u32>( state.range( 0 ) );
    RunConversion(
        state,
        SquareDesc(
            nDimension,
            image_pixel_format_t::RGBA8_UNORM,
            image_color_space_t::SRGB,
            image_alpha_mode_t::STRAIGHT ),
        SquareDesc(
            nDimension,
            image_pixel_format_t::RGBA32_FLOAT,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::STRAIGHT ) );
}

void BM_ImageConvert_Rgba32LinearToRgba8Srgb( benchmark::State &state )
{
    const u32 nDimension = static_cast<u32>( state.range( 0 ) );
    RunConversion(
        state,
        SquareDesc(
            nDimension,
            image_pixel_format_t::RGBA32_FLOAT,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::STRAIGHT ),
        SquareDesc(
            nDimension,
            image_pixel_format_t::RGBA8_UNORM,
            image_color_space_t::SRGB,
            image_alpha_mode_t::STRAIGHT ) );
}

void BM_ImageConvert_Rgba8Swizzle( benchmark::State &state )
{
    const u32 nDimension = static_cast<u32>( state.range( 0 ) );
    const image_convert_options_t options{
        {
            image_channel_t::BLUE,
            image_channel_t::GREEN,
            image_channel_t::RED,
            image_channel_t::ALPHA
        }
    };
    const image_desc_t desc = SquareDesc(
        nDimension,
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT );
    RunConversion( state, desc, desc, options );
}

void BM_ImageConvert_Rgba8Premultiply( benchmark::State &state )
{
    const u32 nDimension = static_cast<u32>( state.range( 0 ) );
    RunConversion(
        state,
        SquareDesc(
            nDimension,
            image_pixel_format_t::RGBA8_UNORM,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::STRAIGHT ),
        SquareDesc(
            nDimension,
            image_pixel_format_t::RGBA8_UNORM,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::PREMULTIPLIED ) );
}

} // namespace

#define CYPHER_IMAGE_CONVERT_BENCHMARK( functionName ) \
    BENCHMARK( functionName )                             \
        ->Arg( 256 )                                      \
        ->Arg( 1024 )                                     \
        ->Arg( 2048 )                                     \
        ->Arg( 4096 )                                     \
        ->MinTime( 0.05 )                                 \
        ->Repetitions( 5 )                                \
        ->ReportAggregatesOnly( true )

CYPHER_IMAGE_CONVERT_BENCHMARK( BM_ImageConvert_Rgba8ToRgba16 );
CYPHER_IMAGE_CONVERT_BENCHMARK( BM_ImageConvert_Rgba8SrgbToRgba32Linear );
CYPHER_IMAGE_CONVERT_BENCHMARK( BM_ImageConvert_Rgba32LinearToRgba8Srgb );
CYPHER_IMAGE_CONVERT_BENCHMARK( BM_ImageConvert_Rgba8Swizzle );
CYPHER_IMAGE_CONVERT_BENCHMARK( BM_ImageConvert_Rgba8Premultiply );

#undef CYPHER_IMAGE_CONVERT_BENCHMARK
