//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/RenderSystem/Image/CypherCommon_ImageMip_Bench.cpp
//  Purpose: Measures generation of one area-filtered mip level.
//  Details: The benchmark isolates ImageMip dispatch and its box resize kernel;
//           complete chain allocation, conversion, and compression are excluded.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageMip.h"
#include "CypherCommon_ImageSurface.h"
#include "CypherCommon_MemoryOps.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

bool MipBenchCreate(
    image_surface_t &surface,
    u32 nDimension ) noexcept
{
    const image_desc_t desc{
        { nDimension, nDimension, 1u },
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT
    };
    return ImageSurface_Create(
               &surface,
               Allocator_GetSystem(),
               desc,
               image_surface_init_t::UNINITIALIZED,
               64u ) == image_surface_status_t::OK;
}

void BM_ImageMip_GenerateRgba32NextLevel( benchmark::State &state )
{
    const u32 nSource = static_cast<u32>( state.range( 0 ) );
    image_surface_t source{};
    image_surface_t destination{};
    if ( !MipBenchCreate( source, nSource ) ||
         !MipBenchCreate( destination, nSource / 2u ) ) {
        state.SkipWithError( "Could not allocate mip benchmark surfaces" );
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
        const image_mip_result_t result = ImageMip_GenerateNextLevel(
            destinationView,
            sourceView );
        if ( result.status != image_mip_status_t::OK ) {
            state.SkipWithError( "Mip generation failed during measurement" );
            break;
        }
        benchmark::ClobberMemory();
    }

    const int64_t cSourcePixels =
        static_cast<int64_t>( nSource ) * nSource;
    const int64_t cDestinationPixels =
        static_cast<int64_t>( nSource / 2u ) * ( nSource / 2u );
    state.SetItemsProcessed( state.iterations() * cDestinationPixels );
    state.SetBytesProcessed(
        state.iterations() *
        ( cSourcePixels + cDestinationPixels ) * 16 );
}

} // namespace

BENCHMARK( BM_ImageMip_GenerateRgba32NextLevel )
    ->Arg( 1024 )
    ->Arg( 2048 )
    ->Arg( 4096 )
    ->MinTime( 0.05 )
    ->Repetitions( 5 )
    ->ReportAggregatesOnly( true );
