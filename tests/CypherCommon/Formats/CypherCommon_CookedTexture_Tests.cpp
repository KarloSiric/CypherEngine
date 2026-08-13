//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Formats/CypherCommon_CookedTexture_Tests.cpp
//  Purpose: Tests the backend-neutral cooked texture resource contract.
//  Details: Covers canonical mip layout, deterministic serialization, format
//           policy, malformed chains, content damage, and transactional reads.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CookedTexture.h"

#include "CypherCommon_MemoryOps.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace cypher::common;

namespace
{

struct texture_fixture_t {
    byte mip0[32]{};
    byte mip1[8]{};
    byte mip2[4]{};
    cooked_texture_desc_t texture{};
    cooked_texture_mip_source_t mips[3]{};

    texture_fixture_t() noexcept
    {
        for ( usize iByte = 0u; iByte < sizeof( mip0 ); ++iByte ) {
            mip0[iByte] = static_cast<byte>( iByte );
        }
        for ( usize iByte = 0u; iByte < sizeof( mip1 ); ++iByte ) {
            mip1[iByte] = static_cast<byte>( 64u + iByte );
        }
        for ( usize iByte = 0u; iByte < sizeof( mip2 ); ++iByte ) {
            mip2[iByte] = static_cast<byte>( 128u + iByte );
        }

        texture.flags = COOKED_TEXTURE_FLAG_GENERATED_MIPS;
        texture.nWidth = 4u;
        texture.nHeight = 2u;
        texture.nMipLevels = 3u;
        mips[0] = { 4u, 2u, 1u, 16u, { mip0, sizeof( mip0 ) } };
        mips[1] = { 2u, 1u, 1u, 8u, { mip1, sizeof( mip1 ) } };
        mips[2] = { 1u, 1u, 1u, 4u, { mip2, sizeof( mip2 ) } };
    }
};

} // namespace

TEST_CASE( "Cooked textures round trip through canonical CYRS files",
           "[CypherCommon][Formats][CookedTexture]" )
{
    texture_fixture_t fixture{};
    const usize cbRequired = CookedTexture_RequiredSize(
        fixture.texture,
        { fixture.mips, 3u } );
    REQUIRE( cbRequired > CY_COOKED_RESOURCE_HEADER_SIZE );

    std::vector<byte> file( cbRequired );
    const content_hash_t sourceHash = ContentHash_String(
        { "textures/wall.cytex", 19u } );
    const cooked_texture_result_t written = CookedTexture_Write(
        fixture.texture,
        { fixture.mips, 3u },
        sourceHash,
        { file.data(), file.size() } );
    REQUIRE( CookedTexture_Succeeded( written ) );
    REQUIRE( written.cbWritten == cbRequired );

    cooked_texture_view_t view{};
    const cooked_texture_result_t read = CookedTexture_Read(
        { file.data(), file.size() },
        &view );
    REQUIRE( CookedTexture_Succeeded( read ) );
    REQUIRE( view.desc.dimension == render_texture_dimension_t::TEXTURE_2D );
    REQUIRE( view.desc.pixelFormat ==
             render_texture_pixel_format_t::RGBA8_SRGB );
    REQUIRE( view.desc.usage == render_texture_usage_t::COLOR );
    REQUIRE( view.desc.colorSpace == render_texture_color_space_t::SRGB );
    REQUIRE( view.nMipLevels == 3u );
    REQUIRE( ContentHash_Equals( view.sourceHash, sourceHash ) );

    const cooked_texture_mip_view_t *pMip0 = CookedTexture_FindMip( view, 0u );
    const cooked_texture_mip_view_t *pMip1 = CookedTexture_FindMip( view, 1u );
    const cooked_texture_mip_view_t *pMip2 = CookedTexture_FindMip( view, 2u );
    REQUIRE( pMip0 != nullptr );
    REQUIRE( pMip1 != nullptr );
    REQUIRE( pMip2 != nullptr );
    REQUIRE( pMip0->nWidth == 4u );
    REQUIRE( pMip0->nHeight == 2u );
    REQUIRE( pMip0->cbRowPitch == 16u );
    REQUIRE( pMip2->nWidth == 1u );
    REQUIRE( pMip2->nHeight == 1u );
    REQUIRE( Cy_MemEqual(
        pMip0->pixels.pData,
        fixture.mip0,
        sizeof( fixture.mip0 ) ) );
    REQUIRE( Cy_MemEqual(
        pMip1->pixels.pData,
        fixture.mip1,
        sizeof( fixture.mip1 ) ) );
    REQUIRE( Cy_MemEqual(
        pMip2->pixels.pData,
        fixture.mip2,
        sizeof( fixture.mip2 ) ) );
    REQUIRE( CookedTexture_FindMip( view, 3u ) == nullptr );
}

TEST_CASE( "Cooked texture writers are deterministic",
           "[CypherCommon][Formats][CookedTexture][Determinism]" )
{
    texture_fixture_t fixture{};
    const usize cbRequired = CookedTexture_RequiredSize(
        fixture.texture,
        { fixture.mips, 3u } );
    std::vector<byte> first( cbRequired, 0xA5u );
    std::vector<byte> second( cbRequired, 0x5Au );

    REQUIRE( CookedTexture_Succeeded( CookedTexture_Write(
        fixture.texture,
        { fixture.mips, 3u },
        {},
        { first.data(), first.size() } ) ) );
    REQUIRE( CookedTexture_Succeeded( CookedTexture_Write(
        fixture.texture,
        { fixture.mips, 3u },
        {},
        { second.data(), second.size() } ) ) );
    REQUIRE( Cy_MemEqual( first.data(), second.data(), cbRequired ) );
}

TEST_CASE( "Cooked texture metadata rejects incompatible semantics",
           "[CypherCommon][Formats][CookedTexture][Validation]" )
{
    texture_fixture_t fixture{};
    cooked_texture_desc_t invalid = fixture.texture;
    invalid.usage = render_texture_usage_t::NORMAL;
    REQUIRE( CookedTexture_RequiredSize(
                 invalid,
                 { fixture.mips, 3u } ) == 0u );

    invalid = fixture.texture;
    invalid.pixelFormat = render_texture_pixel_format_t::RGBA8_UNORM;
    REQUIRE( CookedTexture_RequiredSize(
                 invalid,
                 { fixture.mips, 3u } ) == 0u );

    invalid = fixture.texture;
    invalid.nMipLevels = 2u;
    REQUIRE( CookedTexture_RequiredSize(
                 invalid,
                 { fixture.mips, 2u } ) == 0u );

    cooked_texture_mip_source_t badMips[3]{
        fixture.mips[0],
        fixture.mips[1],
        fixture.mips[2]
    };
    badMips[1].cbRowPitch = 12u;
    REQUIRE( CookedTexture_RequiredSize(
                 fixture.texture,
                 { badMips, 3u } ) == 0u );
}

TEST_CASE( "Cooked texture readers reject damaged files transactionally",
           "[CypherCommon][Formats][CookedTexture][Failure]" )
{
    texture_fixture_t fixture{};
    const usize cbRequired = CookedTexture_RequiredSize(
        fixture.texture,
        { fixture.mips, 3u } );
    std::vector<byte> file( cbRequired );
    REQUIRE( CookedTexture_Succeeded( CookedTexture_Write(
        fixture.texture,
        { fixture.mips, 3u },
        {},
        { file.data(), file.size() } ) ) );

    cooked_texture_view_t output{};
    output.nMipLevels = 77u;
    file.back() ^= static_cast<byte>( 1u );
    const cooked_texture_result_t damaged = CookedTexture_Read(
        { file.data(), file.size() },
        &output );
    REQUIRE( damaged.status == cooked_texture_status_t::RESOURCE_ERROR );
    REQUIRE( damaged.resourceStatus ==
             cooked_resource_status_t::CONTENT_HASH_MISMATCH );
    REQUIRE( output.nMipLevels == 77u );

    REQUIRE( CookedTexture_Read(
                 { file.data(), file.size() - 1u },
                 &output ).status ==
             cooked_texture_status_t::RESOURCE_ERROR );
    REQUIRE( output.nMipLevels == 77u );
}

TEST_CASE( "Cooked texture helpers describe canonical pixel storage",
           "[CypherCommon][Formats][CookedTexture][Helpers]" )
{
    REQUIRE( CookedTexture_BytesPerPixel(
                 render_texture_pixel_format_t::RGBA8_UNORM ) == 4u );
    REQUIRE( CookedTexture_BytesPerPixel(
                 render_texture_pixel_format_t::RGBA8_SRGB ) == 4u );
    REQUIRE( CookedTexture_BytesPerPixel(
                 render_texture_pixel_format_t::RGBA32_FLOAT ) == 16u );
    REQUIRE( CookedTexture_BytesPerPixel(
                 static_cast<render_texture_pixel_format_t>( 99u ) ) == 0u );
    REQUIRE( CookedTexture_FullMipCount( 1u, 1u ) == 1u );
    REQUIRE( CookedTexture_FullMipCount( 4u, 2u ) == 3u );
    REQUIRE( CookedTexture_FullMipCount( 0u, 2u ) == 0u );
    REQUIRE( CookedTexture_MetadataSize( 3u ) == 160u );
}
