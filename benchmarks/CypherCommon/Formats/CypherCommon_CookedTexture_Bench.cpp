//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Formats/CypherCommon_CookedTexture_Bench.cpp
//  Purpose: Benchmarks cooked texture serialization and validation.
//  Details: Measures complete multi-mip CYTX writes and strict borrowed-view
//           reads over representative small and large RGBA8 texture resources.
//           Image decoding and mip generation are separate compiler stages.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CookedTexture.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <vector>

using namespace cypher::common;

namespace
{

struct texture_fixture_t {
    cooked_texture_desc_t texture{};
    std::vector<std::vector<byte>> pixelStorage{};
    std::vector<cooked_texture_mip_source_t> mips{};
    std::vector<byte> file{};
    usize cbPixels{ 0u };
};

texture_fixture_t MakeTextureFixture( u32 nExtent )
{
    texture_fixture_t fixture{};
    const u32 nMipLevels = CookedTexture_FullMipCount(
        nExtent,
        nExtent );

    fixture.texture.pixelFormat =
        render_texture_pixel_format_t::RGBA8_SRGB;
    fixture.texture.usage = render_texture_usage_t::COLOR;
    fixture.texture.colorSpace = render_texture_color_space_t::SRGB;
    fixture.texture.flags = COOKED_TEXTURE_FLAG_GENERATED_MIPS;
    fixture.texture.nWidth = nExtent;
    fixture.texture.nHeight = nExtent;
    fixture.texture.nMipLevels = nMipLevels;

    fixture.pixelStorage.resize( nMipLevels );
    fixture.mips.resize( nMipLevels );

    u32 nWidth = nExtent;
    u32 nHeight = nExtent;
    for ( u32 iMip = 0u; iMip < nMipLevels; ++iMip ) {
        const usize cbMip = static_cast<usize>( nWidth ) * nHeight * 4u;
        std::vector<byte> &pixels = fixture.pixelStorage[iMip];
        pixels.resize( cbMip );
        for ( usize iByte = 0u; iByte < cbMip; ++iByte ) {
            pixels[iByte] = static_cast<byte>(
                ( iByte + static_cast<usize>( iMip ) * 31u ) & 0xFFu );
        }

        fixture.mips[iMip] = {
            nWidth,
            nHeight,
            1u,
            nWidth * 4u,
            { pixels.data(), pixels.size() }
        };
        fixture.cbPixels += cbMip;
        nWidth = std::max( 1u, nWidth / 2u );
        nHeight = std::max( 1u, nHeight / 2u );
    }

    const span_t<const cooked_texture_mip_source_t> mipSpan{
        fixture.mips.data(),
        fixture.mips.size()
    };
    const usize cbFile = CookedTexture_RequiredSize(
        fixture.texture,
        mipSpan );
    fixture.file.resize( cbFile );
    const cooked_texture_result_t written = CookedTexture_Write(
        fixture.texture,
        mipSpan,
        {},
        { fixture.file.data(), fixture.file.size() } );
    if ( !CookedTexture_Succeeded( written ) ) {
        fixture.file.clear();
    }
    return fixture;
}

void BM_CookedTextureWrite( benchmark::State &state )
{
    texture_fixture_t fixture = MakeTextureFixture(
        static_cast<u32>( state.range( 0 ) ) );
    if ( fixture.file.empty() ) {
        state.SkipWithError( "failed to create cooked texture fixture" );
        return;
    }

    const span_t<const cooked_texture_mip_source_t> mipSpan{
        fixture.mips.data(),
        fixture.mips.size()
    };
    for ( auto _ : state ) {
        const cooked_texture_result_t result = CookedTexture_Write(
            fixture.texture,
            mipSpan,
            {},
            { fixture.file.data(), fixture.file.size() } );
        benchmark::DoNotOptimize( static_cast<u8>( result.status ) );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        state.iterations() * static_cast<i64>( fixture.cbPixels ) );
}

void BM_CookedTextureRead( benchmark::State &state )
{
    texture_fixture_t fixture = MakeTextureFixture(
        static_cast<u32>( state.range( 0 ) ) );
    if ( fixture.file.empty() ) {
        state.SkipWithError( "failed to create cooked texture fixture" );
        return;
    }

    for ( auto _ : state ) {
        cooked_texture_view_t view{};
        const cooked_texture_result_t result = CookedTexture_Read(
            { fixture.file.data(), fixture.file.size() },
            &view );
        benchmark::DoNotOptimize( static_cast<u8>( result.status ) );
        benchmark::DoNotOptimize( view.nMipLevels );
    }
    state.SetBytesProcessed(
        state.iterations() * static_cast<i64>( fixture.file.size() ) );
}

} // namespace

BENCHMARK( BM_CookedTextureWrite )->Arg( 64 )->Arg( 1024 );
BENCHMARK( BM_CookedTextureRead )->Arg( 64 )->Arg( 1024 );
