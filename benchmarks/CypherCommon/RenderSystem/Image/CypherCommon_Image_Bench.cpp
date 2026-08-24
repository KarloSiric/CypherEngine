//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/RenderSystem/Image/CypherCommon_Image_Bench.cpp
//  Purpose: Benchmarks image metadata, layout, validation, and bounded access.
//  Details: Measurements cover the small operations used repeatedly by image
//           processing and editor code without including codec or allocation cost.
//
//  History:
//  - Created by Karlo Siric on 2026-08-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageSurface.h"

#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>

using namespace cypher::common;

namespace
{

constexpr u32 kImageWidth = 256u;
constexpr u32 kImageHeight = 256u;
constexpr usize kRowPitch = kImageWidth * 4u;
constexpr usize kImageSize = kRowPitch * kImageHeight;

image_desc_t SquareRgba8Desc( u32 nDimension ) noexcept
{
    return {
        { nDimension, nDimension, 1u },
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT
    };
}

struct image_bench_data_t {
    std::array<byte, kImageSize> pixels{};
    image_view_t view{};

    image_bench_data_t() noexcept
        : view{
              {
                  { kImageWidth, kImageHeight, 1u },
                  image_pixel_format_t::RGBA8_UNORM,
                  image_color_space_t::SRGB,
                  image_alpha_mode_t::STRAIGHT
              },
              { pixels.data(), pixels.size() },
              kRowPitch,
              kImageSize }
    {
    }
};

image_bench_data_t &ImageBenchData()
{
    static image_bench_data_t data{};
    return data;
}

void BM_ImageFormat_GetInfo( benchmark::State &state )
{
    for ( auto _ : state ) {
        for ( usize iFormat = 1u;
              iFormat < static_cast<usize>( image_pixel_format_t::COUNT );
              ++iFormat ) {
            benchmark::DoNotOptimize( ImageFormat_GetInfo(
                static_cast<image_pixel_format_t>( iFormat ) ) );
        }
    }
}

void BM_ImageFormat_CalculateLayout( benchmark::State &state )
{
    const image_desc_t desc = ImageBenchData().view.desc;
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( ImageFormat_CalculateLayout( desc, 256u ) );
    }
}

void BM_ImageView_Validate( benchmark::State &state )
{
    const image_view_t &view = ImageBenchData().view;
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( ImageView_Validate( view ) );
    }
}

void BM_ImageView_GetEveryRow( benchmark::State &state )
{
    const image_view_t &view = ImageBenchData().view;
    for ( auto _ : state ) {
        usize nAccum = 0u;
        for ( u32 iRow = 0u; iRow < kImageHeight; ++iRow ) {
            byte_span_t row = ImageView_GetRow( view, iRow, 0u );
            nAccum += row.nCount;
            benchmark::DoNotOptimize( row.pData );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_ImageView_Get4096Pixels( benchmark::State &state )
{
    const image_view_t &view = ImageBenchData().view;
    for ( auto _ : state ) {
        usize nAccum = 0u;
        for ( u32 iPixel = 0u; iPixel < 4096u; ++iPixel ) {
            const u32 iColumn = ( iPixel * 37u ) & 255u;
            const u32 iRow = ( iPixel * 17u ) & 255u;
            byte_span_t pixel =
                ImageView_GetPixel( view, iColumn, iRow, 0u );
            nAccum += pixel.nCount;
            benchmark::DoNotOptimize( pixel.pData );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_ImageSurface_GetView( benchmark::State &state )
{
    image_surface_t surface{};
    const image_surface_status_t createStatus = ImageSurface_Create(
        &surface,
        Allocator_GetSystem(),
        ImageBenchData().view.desc,
        image_surface_init_t::UNINITIALIZED,
        64u );
    if ( createStatus != image_surface_status_t::OK ) {
        state.SkipWithError( "Could not allocate image benchmark surface" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( ImageSurface_GetView( &surface ) );
    }
}

void BM_ImageSurface_CreateDestroy( benchmark::State &state )
{
    const image_desc_t desc = ImageBenchData().view.desc;
    for ( auto _ : state ) {
        image_surface_t surface{};
        const image_surface_status_t status = ImageSurface_Create(
            &surface,
            Allocator_GetSystem(),
            desc,
            image_surface_init_t::UNINITIALIZED,
            64u );
        if ( status != image_surface_status_t::OK ) {
            state.SkipWithError( "Could not allocate image benchmark surface" );
            break;
        }
        benchmark::DoNotOptimize( surface.allocation.pData );
        ImageSurface_Destroy( &surface );
    }

}

void BM_ImageSurface_CreateZeroedDestroy( benchmark::State &state )
{
    const u32 nDimension = static_cast<u32>( state.range( 0 ) );
    const image_desc_t desc = SquareRgba8Desc( nDimension );
    const image_layout_result_t layout =
        ImageFormat_CalculateLayout( desc, 64u );
    for ( auto _ : state ) {
        image_surface_t surface{};
        const image_surface_status_t status = ImageSurface_Create(
            &surface,
            Allocator_GetSystem(),
            desc,
            image_surface_init_t::ZEROED,
            64u );
        if ( status != image_surface_status_t::OK ) {
            state.SkipWithError( "Could not allocate image benchmark surface" );
            break;
        }
        benchmark::DoNotOptimize( surface.allocation.pData );
        benchmark::ClobberMemory();
        ImageSurface_Destroy( &surface );
    }

    state.SetBytesProcessed(
        state.iterations() *
        static_cast<int64_t>( layout.layout.cbTotalSize ) );
}

void BM_ImageSurface_ZeroPixels( benchmark::State &state )
{
    const u32 nDimension = static_cast<u32>( state.range( 0 ) );
    image_surface_t surface{};
    const image_surface_status_t createStatus = ImageSurface_Create(
        &surface,
        Allocator_GetSystem(),
        SquareRgba8Desc( nDimension ),
        image_surface_init_t::UNINITIALIZED,
        64u );
    if ( createStatus != image_surface_status_t::OK ) {
        state.SkipWithError( "Could not allocate image benchmark surface" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( ImageSurface_ZeroPixels( &surface ) );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        state.iterations() *
        static_cast<int64_t>( surface.layout.cbTotalSize ) );
}

void BM_ImageSurface_CreateFromView( benchmark::State &state )
{
    const u32 nDimension = static_cast<u32>( state.range( 0 ) );
    image_surface_t sourceSurface{};
    const image_surface_status_t sourceStatus = ImageSurface_Create(
        &sourceSurface,
        Allocator_GetSystem(),
        SquareRgba8Desc( nDimension ),
        image_surface_init_t::ZEROED,
        64u );
    if ( sourceStatus != image_surface_status_t::OK ) {
        state.SkipWithError( "Could not allocate source image surface" );
        return;
    }

    const image_surface_t &immutableSource = sourceSurface;
    const const_image_view_t source =
        ImageSurface_GetView( &immutableSource );
    for ( auto _ : state ) {
        image_surface_t surface{};
        const image_surface_status_t status = ImageSurface_CreateFromView(
            &surface,
            Allocator_GetSystem(),
            source,
            64u );
        if ( status != image_surface_status_t::OK ) {
            state.SkipWithError( "Could not copy image benchmark surface" );
            break;
        }
        benchmark::DoNotOptimize( surface.allocation.pData );
        benchmark::ClobberMemory();
        ImageSurface_Destroy( &surface );
    }

    state.SetBytesProcessed(
        state.iterations() *
        static_cast<int64_t>( sourceSurface.layout.cbTotalSize ) );
}

void BM_ImageSurface_CopyIntoExisting( benchmark::State &state )
{
    const u32 nDimension = static_cast<u32>( state.range( 0 ) );
    image_surface_t sourceSurface{};
    image_surface_t destinationSurface{};
    const image_desc_t desc = SquareRgba8Desc( nDimension );
    if ( ImageSurface_Create(
             &sourceSurface,
             Allocator_GetSystem(),
             desc,
             image_surface_init_t::ZEROED,
             64u ) != image_surface_status_t::OK ||
         ImageSurface_Create(
             &destinationSurface,
             Allocator_GetSystem(),
             desc,
             image_surface_init_t::UNINITIALIZED,
             64u ) != image_surface_status_t::OK ) {
        state.SkipWithError( "Could not allocate reusable image surfaces" );
        return;
    }

    const image_surface_t &immutableSource = sourceSurface;
    const const_image_view_t source =
        ImageSurface_GetView( &immutableSource );
    for ( auto _ : state ) {
        const image_surface_status_t status =
            ImageSurface_CopyFromView( &destinationSurface, source );
        if ( status != image_surface_status_t::OK ) {
            state.SkipWithError( "Could not copy into reusable image surface" );
            break;
        }
        benchmark::DoNotOptimize( destinationSurface.allocation.pData );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        state.iterations() *
        static_cast<int64_t>( sourceSurface.layout.cbTotalSize ) );
}

void BM_ImageSurface_RecreateWithinCapacity( benchmark::State &state )
{
    const u32 nDimension = static_cast<u32>( state.range( 0 ) );
    image_surface_t surface{};
    const image_desc_t desc = SquareRgba8Desc( nDimension );
    if ( ImageSurface_Create(
             &surface,
             Allocator_GetSystem(),
             desc,
             image_surface_init_t::UNINITIALIZED,
             64u ) != image_surface_status_t::OK ) {
        state.SkipWithError( "Could not allocate reusable image surface" );
        return;
    }

    void *pOriginalPixels = surface.allocation.pData;
    for ( auto _ : state ) {
        const image_surface_status_t status = ImageSurface_Recreate(
            &surface,
            desc,
            image_surface_init_t::UNINITIALIZED,
            64u );
        if ( status != image_surface_status_t::OK ||
             surface.allocation.pData != pOriginalPixels ) {
            state.SkipWithError( "Image surface unexpectedly reallocated" );
            break;
        }
        benchmark::DoNotOptimize( surface.layout.cbTotalSize );
    }
}

} // namespace

BENCHMARK( BM_ImageFormat_GetInfo );
BENCHMARK( BM_ImageFormat_CalculateLayout );
BENCHMARK( BM_ImageView_Validate );
BENCHMARK( BM_ImageView_GetEveryRow );
BENCHMARK( BM_ImageView_Get4096Pixels );
BENCHMARK( BM_ImageSurface_GetView );
BENCHMARK( BM_ImageSurface_CreateDestroy );
BENCHMARK( BM_ImageSurface_CreateZeroedDestroy )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 );
BENCHMARK( BM_ImageSurface_ZeroPixels )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 );
BENCHMARK( BM_ImageSurface_CreateFromView )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 );
BENCHMARK( BM_ImageSurface_CopyIntoExisting )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 );
BENCHMARK( BM_ImageSurface_RecreateWithinCapacity )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 );
