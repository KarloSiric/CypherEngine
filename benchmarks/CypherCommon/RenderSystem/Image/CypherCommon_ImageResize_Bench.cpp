//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/RenderSystem/Image/CypherCommon_ImageResize_Bench.cpp
//  Purpose: Measures nearest, linear, and area-box image resizing.
//  Details: Surface allocation and initialization stay outside timed loops so
//           results isolate scaling kernels across editor-relevant resolutions.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageResize.h"
#include "CypherCommon_ImageSurface.h"
#include "CypherCommon_MemoryOps.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

image_desc_t ResizeBenchDesc(
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

bool ResizeBenchCreate(
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

void ResizeBenchSetWork(
    benchmark::State &state,
    const image_desc_t &source,
    const image_desc_t &destination )
{
    const image_format_info_t *pSourceInfo =
        ImageFormat_GetInfo( source.pixelFormat );
    const image_format_info_t *pDestinationInfo =
        ImageFormat_GetInfo( destination.pixelFormat );
    const int64_t cSourcePixels =
        static_cast<int64_t>( source.extent.nWidth ) *
        static_cast<int64_t>( source.extent.nHeight ) *
        static_cast<int64_t>( source.extent.nDepth );
    const int64_t cDestinationPixels =
        static_cast<int64_t>( destination.extent.nWidth ) *
        static_cast<int64_t>( destination.extent.nHeight ) *
        static_cast<int64_t>( destination.extent.nDepth );
    const int64_t cbLogicalTraffic =
        cSourcePixels * pSourceInfo->cbPixel +
        cDestinationPixels * pDestinationInfo->cbPixel;

    state.SetItemsProcessed( state.iterations() * cDestinationPixels );
    state.SetBytesProcessed( state.iterations() * cbLogicalTraffic );
    state.counters["source_pixels"] =
        static_cast<double>( cSourcePixels );
    state.counters["destination_pixels"] =
        static_cast<double>( cDestinationPixels );
}

void ResizeBenchRun(
    benchmark::State &state,
    const image_desc_t &sourceDesc,
    const image_desc_t &destinationDesc,
    image_resize_filter_t filter )
{
    image_surface_t source{};
    image_surface_t destination{};
    if ( !ResizeBenchCreate( source, sourceDesc ) ||
         !ResizeBenchCreate( destination, destinationDesc ) ) {
        state.SkipWithError( "Could not allocate resize benchmark surfaces" );
        return;
    }
    Cy_MemSet(
        source.allocation.pData,
        0x3Fu,
        source.layout.cbTotalSize );

    const const_image_view_t sourceView = ImageSurface_GetView(
        static_cast<const image_surface_t *>( &source ) );
    const image_view_t destinationView = ImageSurface_GetView( &destination );
    for ( auto _ : state ) {
        const image_resize_status_t status = ImageResize(
            destinationView,
            sourceView,
            filter );
        if ( status != image_resize_status_t::OK ) {
            state.SkipWithError( "Image resize failed during measurement" );
            break;
        }
        benchmark::ClobberMemory();
    }
    ResizeBenchSetWork( state, sourceDesc, destinationDesc );
}

void BM_ImageResize_Rgba8NearestHalf( benchmark::State &state )
{
    const u32 nSource = static_cast<u32>( state.range( 0 ) );
    ResizeBenchRun(
        state,
        ResizeBenchDesc(
            nSource,
            image_pixel_format_t::RGBA8_UNORM,
            image_color_space_t::SRGB,
            image_alpha_mode_t::STRAIGHT ),
        ResizeBenchDesc(
            nSource / 2u,
            image_pixel_format_t::RGBA8_UNORM,
            image_color_space_t::SRGB,
            image_alpha_mode_t::STRAIGHT ),
        image_resize_filter_t::NEAREST );
}

void BM_ImageResize_Rgba32LinearHalf( benchmark::State &state )
{
    const u32 nSource = static_cast<u32>( state.range( 0 ) );
    ResizeBenchRun(
        state,
        ResizeBenchDesc(
            nSource,
            image_pixel_format_t::RGBA32_FLOAT,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::STRAIGHT ),
        ResizeBenchDesc(
            nSource / 2u,
            image_pixel_format_t::RGBA32_FLOAT,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::STRAIGHT ),
        image_resize_filter_t::LINEAR );
}

void BM_ImageResize_Rgba32BoxHalf( benchmark::State &state )
{
    const u32 nSource = static_cast<u32>( state.range( 0 ) );
    ResizeBenchRun(
        state,
        ResizeBenchDesc(
            nSource,
            image_pixel_format_t::RGBA32_FLOAT,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::STRAIGHT ),
        ResizeBenchDesc(
            nSource / 2u,
            image_pixel_format_t::RGBA32_FLOAT,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::STRAIGHT ),
        image_resize_filter_t::BOX );
}

void BM_ImageResize_Rgba32LinearDouble( benchmark::State &state )
{
    const u32 nSource = static_cast<u32>( state.range( 0 ) ) / 2u;
    ResizeBenchRun(
        state,
        ResizeBenchDesc(
            nSource,
            image_pixel_format_t::RGBA32_FLOAT,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::STRAIGHT ),
        ResizeBenchDesc(
            nSource * 2u,
            image_pixel_format_t::RGBA32_FLOAT,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::STRAIGHT ),
        image_resize_filter_t::LINEAR );
}

} // namespace

#define CYPHER_IMAGE_RESIZE_BENCHMARK( functionName ) \
    BENCHMARK( functionName )                            \
        ->Arg( 1024 )                                    \
        ->Arg( 2048 )                                    \
        ->Arg( 4096 )                                    \
        ->MinTime( 0.05 )                                \
        ->Repetitions( 5 )                               \
        ->ReportAggregatesOnly( true )

CYPHER_IMAGE_RESIZE_BENCHMARK( BM_ImageResize_Rgba8NearestHalf );
CYPHER_IMAGE_RESIZE_BENCHMARK( BM_ImageResize_Rgba32LinearHalf );
CYPHER_IMAGE_RESIZE_BENCHMARK( BM_ImageResize_Rgba32BoxHalf );
CYPHER_IMAGE_RESIZE_BENCHMARK( BM_ImageResize_Rgba32LinearDouble );

#undef CYPHER_IMAGE_RESIZE_BENCHMARK
