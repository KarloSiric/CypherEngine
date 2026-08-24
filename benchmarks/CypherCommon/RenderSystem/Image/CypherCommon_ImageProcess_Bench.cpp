//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/RenderSystem/Image/CypherCommon_ImageProcess_Bench.cpp
//  Purpose: Measures allocation-free image copy, fill, flip, and rotation work.
//  Details: Storage is allocated before timing so results isolate processing
//           bandwidth across editor-relevant image sizes and pitched layouts.
//
//  History:
//  - Created by Karlo Siric on 2026-08-17
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageProcess.h"
#include "CypherCommon_ImageSurface.h"
#include "CypherCommon_MemoryOps.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

image_desc_t SquareRgba8Desc( u32 nDimension ) noexcept
{
    return {
        { nDimension, nDimension, 1u },
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT
    };
}

bool CreateSurface(
    image_surface_t &surface,
    const image_desc_t &desc,
    usize cbRowAlignment ) noexcept
{
    return ImageSurface_Create(
               &surface,
               Allocator_GetSystem(),
               desc,
               image_surface_init_t::UNINITIALIZED,
               cbRowAlignment ) == image_surface_status_t::OK;
}

void SeedSurface( image_surface_t &surface, byte value ) noexcept
{
    Cy_MemSet(
        surface.allocation.pData,
        value,
        surface.layout.cbTotalSize );
}

void SetLogicalBytesProcessed(
    benchmark::State &state,
    const image_desc_t &desc )
{
    const image_format_info_t *pInfo =
        ImageFormat_GetInfo( desc.pixelFormat );
    const int64_t cbLogicalImage =
        static_cast<int64_t>( desc.extent.nWidth ) *
        static_cast<int64_t>( desc.extent.nHeight ) *
        static_cast<int64_t>( desc.extent.nDepth ) *
        static_cast<int64_t>( pInfo->cbPixel );
    state.SetBytesProcessed( state.iterations() * cbLogicalImage );
}

void SetRegionBytesProcessed(
    benchmark::State &state,
    const image_region_t &region,
    image_pixel_format_t format )
{
    const image_format_info_t *pInfo = ImageFormat_GetInfo( format );
    const int64_t cbRegion =
        static_cast<int64_t>( region.extent.nWidth ) *
        static_cast<int64_t>( region.extent.nHeight ) *
        static_cast<int64_t>( region.extent.nDepth ) *
        static_cast<int64_t>( pInfo->cbPixel );
    state.SetBytesProcessed( state.iterations() * cbRegion );
}

void BM_ImageProcess_Copy( benchmark::State &state )
{
    const image_desc_t desc = SquareRgba8Desc(
        static_cast<u32>( state.range( 0 ) ) );
    image_surface_t source{};
    image_surface_t destination{};
    if ( !CreateSurface( source, desc, 1u ) ||
         !CreateSurface( destination, desc, 1u ) ) {
        state.SkipWithError( "Could not allocate copy benchmark surfaces" );
        return;
    }
    SeedSurface( source, 0x5Au );
    const const_image_view_t sourceView =
        ImageSurface_GetView( static_cast<const image_surface_t *>( &source ) );
    const image_view_t destinationView = ImageSurface_GetView( &destination );

    for ( auto _ : state ) {
        const image_process_status_t status =
            ImageProcess_Copy( destinationView, sourceView );
        if ( status != image_process_status_t::OK ) {
            state.SkipWithError( "Image copy failed during measurement" );
            break;
        }
        benchmark::ClobberMemory();
    }
    SetLogicalBytesProcessed( state, desc );
}

void BM_ImageProcess_CopyPadded( benchmark::State &state )
{
    const u32 nDimension = static_cast<u32>( state.range( 0 ) );
    const image_desc_t desc{
        { nDimension - 17u, nDimension, 1u },
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT
    };
    image_surface_t source{};
    image_surface_t destination{};
    if ( !CreateSurface( source, desc, 256u ) ||
         !CreateSurface( destination, desc, 256u ) ) {
        state.SkipWithError( "Could not allocate padded copy surfaces" );
        return;
    }
    SeedSurface( source, 0xA5u );
    const const_image_view_t sourceView =
        ImageSurface_GetView( static_cast<const image_surface_t *>( &source ) );
    const image_view_t destinationView = ImageSurface_GetView( &destination );

    for ( auto _ : state ) {
        const image_process_status_t status =
            ImageProcess_Copy( destinationView, sourceView );
        if ( status != image_process_status_t::OK ) {
            state.SkipWithError( "Padded image copy failed during measurement" );
            break;
        }
        benchmark::ClobberMemory();
    }
    SetLogicalBytesProcessed( state, desc );
    state.counters["row_padding_bytes"] =
        static_cast<double>( destination.layout.cbRowPitch ) -
        static_cast<double>( desc.extent.nWidth * 4u );
}

void BM_ImageProcess_CopyRegion( benchmark::State &state )
{
    const u32 nDimension = static_cast<u32>( state.range( 0 ) );
    const image_desc_t desc = SquareRgba8Desc( nDimension );
    const u32 iMargin = nDimension / 8u;
    const image_region_t region{
        { iMargin, iMargin, 0u },
        { nDimension - iMargin * 2u,
          nDimension - iMargin * 2u,
          1u }
    };
    image_surface_t source{};
    image_surface_t destination{};
    if ( !CreateSurface( source, desc, 64u ) ||
         !CreateSurface( destination, desc, 64u ) ) {
        state.SkipWithError( "Could not allocate region copy surfaces" );
        return;
    }
    SeedSurface( source, 0x92u );
    const const_image_view_t sourceView =
        ImageSurface_GetView( static_cast<const image_surface_t *>( &source ) );
    const image_view_t destinationView = ImageSurface_GetView( &destination );

    for ( auto _ : state ) {
        const image_process_status_t status = ImageProcess_CopyRegion(
            destinationView,
            region.origin,
            sourceView,
            region );
        if ( status != image_process_status_t::OK ) {
            state.SkipWithError( "Image region copy failed during measurement" );
            break;
        }
        benchmark::ClobberMemory();
    }
    SetRegionBytesProcessed( state, region, desc.pixelFormat );
}

void BM_ImageProcess_Fill( benchmark::State &state )
{
    const image_desc_t desc = SquareRgba8Desc(
        static_cast<u32>( state.range( 0 ) ) );
    image_surface_t destination{};
    if ( !CreateSurface( destination, desc, 64u ) ) {
        state.SkipWithError( "Could not allocate fill benchmark surface" );
        return;
    }
    const byte pixel[4u]{ 0x31u, 0x72u, 0xA4u, 0xFFu };
    const image_view_t destinationView = ImageSurface_GetView( &destination );

    for ( auto _ : state ) {
        const image_process_status_t status = ImageProcess_Fill(
            destinationView,
            { pixel, sizeof( pixel ) } );
        if ( status != image_process_status_t::OK ) {
            state.SkipWithError( "Image fill failed during measurement" );
            break;
        }
        benchmark::ClobberMemory();
    }
    SetLogicalBytesProcessed( state, desc );
}

void BM_ImageProcess_FillRegion( benchmark::State &state )
{
    const u32 nDimension = static_cast<u32>( state.range( 0 ) );
    const image_desc_t desc = SquareRgba8Desc( nDimension );
    const u32 iMargin = nDimension / 8u;
    const image_region_t region{
        { iMargin, iMargin, 0u },
        { nDimension - iMargin * 2u,
          nDimension - iMargin * 2u,
          1u }
    };
    image_surface_t destination{};
    if ( !CreateSurface( destination, desc, 64u ) ) {
        state.SkipWithError( "Could not allocate region fill surface" );
        return;
    }
    const byte pixel[4u]{ 0x18u, 0x47u, 0xB2u, 0xFFu };
    const image_view_t destinationView = ImageSurface_GetView( &destination );

    for ( auto _ : state ) {
        const image_process_status_t status = ImageProcess_FillRegion(
            destinationView,
            region,
            { pixel, sizeof( pixel ) } );
        if ( status != image_process_status_t::OK ) {
            state.SkipWithError( "Image region fill failed during measurement" );
            break;
        }
        benchmark::ClobberMemory();
    }
    SetRegionBytesProcessed( state, region, desc.pixelFormat );
}

using image_transform_fn_t = image_process_status_t ( * )(
    const image_view_t &,
    const const_image_view_t & ) noexcept;

void BM_ImageProcess_Transform(
    benchmark::State &state,
    image_transform_fn_t pfnTransform )
{
    const image_desc_t desc = SquareRgba8Desc(
        static_cast<u32>( state.range( 0 ) ) );
    image_surface_t source{};
    image_surface_t destination{};
    if ( !CreateSurface( source, desc, 64u ) ||
         !CreateSurface( destination, desc, 64u ) ) {
        state.SkipWithError( "Could not allocate transform benchmark surfaces" );
        return;
    }
    SeedSurface( source, 0x7Bu );
    const const_image_view_t sourceView =
        ImageSurface_GetView( static_cast<const image_surface_t *>( &source ) );
    const image_view_t destinationView = ImageSurface_GetView( &destination );

    for ( auto _ : state ) {
        const image_process_status_t status =
            pfnTransform( destinationView, sourceView );
        if ( status != image_process_status_t::OK ) {
            state.SkipWithError( "Image transform failed during measurement" );
            break;
        }
        benchmark::ClobberMemory();
    }
    SetLogicalBytesProcessed( state, desc );
}

void BM_ImageProcess_FlipHorizontal( benchmark::State &state )
{
    BM_ImageProcess_Transform( state, ImageProcess_FlipHorizontal );
}

void BM_ImageProcess_FlipVertical( benchmark::State &state )
{
    BM_ImageProcess_Transform( state, ImageProcess_FlipVertical );
}

void BM_ImageProcess_Rotate180( benchmark::State &state )
{
    BM_ImageProcess_Transform( state, ImageProcess_Rotate180 );
}

void BM_ImageProcess_Rotate90Clockwise( benchmark::State &state )
{
    BM_ImageProcess_Transform( state, ImageProcess_Rotate90Clockwise );
}

void BM_ImageProcess_Rotate90CounterClockwise( benchmark::State &state )
{
    BM_ImageProcess_Transform(
        state,
        ImageProcess_Rotate90CounterClockwise );
}

void BM_ImageProcess_TransformInPlace(
    benchmark::State &state,
    image_transform_fn_t pfnTransform )
{
    const image_desc_t desc = SquareRgba8Desc(
        static_cast<u32>( state.range( 0 ) ) );
    image_surface_t surface{};
    if ( !CreateSurface( surface, desc, 64u ) ) {
        state.SkipWithError( "Could not allocate in-place transform surface" );
        return;
    }
    SeedSurface( surface, 0x63u );
    const image_view_t view = ImageSurface_GetView( &surface );
    const const_image_view_t sourceView = ImageView_AsConst( view );

    for ( auto _ : state ) {
        const image_process_status_t status =
            pfnTransform( view, sourceView );
        if ( status != image_process_status_t::OK ) {
            state.SkipWithError( "In-place transform failed during measurement" );
            break;
        }
        benchmark::ClobberMemory();
    }
    SetLogicalBytesProcessed( state, desc );
}

void BM_ImageProcess_FlipHorizontalInPlace( benchmark::State &state )
{
    BM_ImageProcess_TransformInPlace(
        state,
        ImageProcess_FlipHorizontal );
}

void BM_ImageProcess_FlipVerticalInPlace( benchmark::State &state )
{
    BM_ImageProcess_TransformInPlace(
        state,
        ImageProcess_FlipVertical );
}

void BM_ImageProcess_Rotate180InPlace( benchmark::State &state )
{
    BM_ImageProcess_TransformInPlace(
        state,
        ImageProcess_Rotate180 );
}

} // namespace

BENCHMARK( BM_ImageProcess_Copy )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 );
BENCHMARK( BM_ImageProcess_CopyPadded )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 );
BENCHMARK( BM_ImageProcess_CopyRegion )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 );
BENCHMARK( BM_ImageProcess_Fill )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 );
BENCHMARK( BM_ImageProcess_FillRegion )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 );
BENCHMARK( BM_ImageProcess_FlipHorizontal )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 )
    ->MinTime( 0.05 );
BENCHMARK( BM_ImageProcess_FlipVertical )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 )
    ->MinTime( 0.05 );
BENCHMARK( BM_ImageProcess_Rotate180 )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 )
    ->MinTime( 0.05 );
BENCHMARK( BM_ImageProcess_Rotate90Clockwise )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 )
    ->MinTime( 0.05 );
BENCHMARK( BM_ImageProcess_Rotate90CounterClockwise )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 )
    ->MinTime( 0.05 );
BENCHMARK( BM_ImageProcess_FlipHorizontalInPlace )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 )
    ->MinTime( 0.05 );
BENCHMARK( BM_ImageProcess_FlipVerticalInPlace )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 )
    ->MinTime( 0.05 );
BENCHMARK( BM_ImageProcess_Rotate180InPlace )
    ->Arg( 256 )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 )
    ->MinTime( 0.05 );
