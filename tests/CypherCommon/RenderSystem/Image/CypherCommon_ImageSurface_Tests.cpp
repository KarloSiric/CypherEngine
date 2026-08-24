//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/RenderSystem/Image/CypherCommon_ImageSurface_Tests.cpp
//  Purpose: Tests allocator-backed ownership of uncompressed image pixels.
//  Details: Coverage protects lifecycle, aligned layouts, deterministic copying,
//           capacity reuse, allocation failure, and ownership transfer.
//
//  History:
//  - Created by Karlo Siric on 2026-08-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageSurface.h"

#include "CypherCommon_Align.h"
#include "CypherCommon_MemoryOps.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

using namespace cypher::common;

namespace
{

image_desc_t TestSurfaceDesc() noexcept
{
    return {
        { 3u, 2u, 2u },
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT
    };
}

void *FailAllocation( void *, usize, usize ) noexcept
{
    return nullptr;
}

struct allocator_probe_t {
    usize cAllocations{ 0u };
    usize cFrees{ 0u };
};

void *ProbeAllocate(
    void *pUserData,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pProbe = static_cast<allocator_probe_t *>( pUserData );
    ++pProbe->cAllocations;
    return Allocator_Allocate(
        Allocator_GetSystem(),
        cbSize,
        nAlignment );
}

void ProbeFree(
    void *pUserData,
    void *pMemory,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pProbe = static_cast<allocator_probe_t *>( pUserData );
    ++pProbe->cFrees;
    Allocator_Free(
        Allocator_GetSystem(),
        pMemory,
        cbSize,
        nAlignment );
}

} // namespace

TEST_CASE( "Image surfaces start empty and destroy idempotently",
           "[CypherCommon][Image][Surface][Lifecycle]" )
{
    image_surface_t surface{};
    REQUIRE( ImageSurface_IsEmpty( &surface ) );
    REQUIRE_FALSE( ImageSurface_IsValid( &surface ) );

    ImageSurface_Destroy( &surface );
    ImageSurface_Destroy( &surface );
    REQUIRE( ImageSurface_IsEmpty( &surface ) );
}

TEST_CASE( "Image surface creation owns an aligned zeroed layout",
           "[CypherCommon][Image][Surface][Create]" )
{
    image_surface_t surface{};
    REQUIRE( ImageSurface_Create(
                 &surface,
                 Allocator_GetSystem(),
                 TestSurfaceDesc(),
                 image_surface_init_t::ZEROED,
                 16u ) == image_surface_status_t::OK );

    REQUIRE( ImageSurface_IsValid( &surface ) );
    REQUIRE_FALSE( ImageSurface_IsEmpty( &surface ) );
    REQUIRE( surface.layout.cbRowPitch == 16u );
    REQUIRE( surface.layout.cbSlicePitch == 32u );
    REQUIRE( surface.layout.cbTotalSize == 64u );
    REQUIRE( surface.cbRowAlignment == 16u );
    REQUIRE( surface.allocation.cbSize == 64u );
    REQUIRE( Cy_AlignIsPointerAligned(
        surface.allocation.pData,
        16u ) );
    REQUIRE( Cy_MemIsZero(
        surface.allocation.pData,
        surface.allocation.cbSize ) );
}

TEST_CASE( "Image surfaces allocate supported texture dimensions through 4K",
           "[CypherCommon][Image][Surface][Scale]" )
{
    constexpr u32 dimensions[] = { 256u, 1024u, 2048u, 4096u };

    for ( const u32 nDimension : dimensions ) {
        CAPTURE( nDimension );
        const image_desc_t desc{
            { nDimension, nDimension, 1u },
            image_pixel_format_t::RGBA8_UNORM,
            image_color_space_t::SRGB,
            image_alpha_mode_t::STRAIGHT
        };
        image_surface_t surface{};
        REQUIRE( ImageSurface_Create(
                     &surface,
                     Allocator_GetSystem(),
                     desc,
                     image_surface_init_t::UNINITIALIZED,
                     256u ) == image_surface_status_t::OK );
        REQUIRE( ImageSurface_IsValid( &surface ) );
        REQUIRE( surface.layout.cbTotalSize ==
                 static_cast<usize>( nDimension ) *
                 static_cast<usize>( nDimension ) * 4u );
        ImageSurface_Destroy( &surface );
    }
}

TEST_CASE( "Image surface creation rejects invalid requests without mutation",
           "[CypherCommon][Image][Surface][Failure]" )
{
    const image_desc_t validDesc = TestSurfaceDesc();
    image_surface_t surface{};

    REQUIRE( ImageSurface_Create(
                 nullptr,
                 Allocator_GetSystem(),
                 validDesc,
                 image_surface_init_t::ZEROED ) ==
             image_surface_status_t::NULL_SURFACE );
    REQUIRE( ImageSurface_Create(
                 &surface,
                 nullptr,
                 validDesc,
                 image_surface_init_t::ZEROED ) ==
             image_surface_status_t::INVALID_ALLOCATOR );
    REQUIRE( ImageSurface_Create(
                 &surface,
                 Allocator_GetSystem(),
                 validDesc,
                 image_surface_init_t::ZEROED,
                 3u ) ==
             image_surface_status_t::INVALID_ROW_ALIGNMENT );
    REQUIRE( ImageSurface_Create(
                 &surface,
                 Allocator_GetSystem(),
                 validDesc,
                 static_cast<image_surface_init_t>( 0xFFu ) ) ==
             image_surface_status_t::INVALID_INITIALIZATION );

    image_desc_t invalidDesc = validDesc;
    invalidDesc.extent.nWidth = 0u;
    REQUIRE( ImageSurface_Create(
                 &surface,
                 Allocator_GetSystem(),
                 invalidDesc,
                 image_surface_init_t::ZEROED ) ==
             image_surface_status_t::INVALID_DESCRIPTOR );

    image_desc_t overflowDesc = validDesc;
    overflowDesc.extent = { CY_U32_MAX, CY_U32_MAX, CY_U32_MAX };
    REQUIRE( ImageSurface_Create(
                 &surface,
                 Allocator_GetSystem(),
                 overflowDesc,
                 image_surface_init_t::ZEROED ) ==
             image_surface_status_t::ARITHMETIC_OVERFLOW );

    const const_image_view_t invalidSource{};
    REQUIRE( ImageSurface_CreateFromView(
                 &surface,
                 Allocator_GetSystem(),
                 invalidSource ) ==
             image_surface_status_t::INVALID_SOURCE_VIEW );
    REQUIRE( ImageSurface_IsEmpty( &surface ) );

    allocator_t failingAllocator = *Allocator_GetSystem();
    failingAllocator.pfnAllocate = FailAllocation;
    REQUIRE( ImageSurface_Create(
                 &surface,
                 &failingAllocator,
                 validDesc,
                 image_surface_init_t::ZEROED ) ==
             image_surface_status_t::ALLOCATION_FAILED );
    REQUIRE( ImageSurface_IsEmpty( &surface ) );

    REQUIRE( ImageSurface_Create(
                 &surface,
                 Allocator_GetSystem(),
                 validDesc,
                 image_surface_init_t::UNINITIALIZED ) ==
             image_surface_status_t::OK );
    REQUIRE( ImageSurface_Create(
                 &surface,
                 Allocator_GetSystem(),
                 validDesc,
                 image_surface_init_t::ZEROED ) ==
             image_surface_status_t::DESTINATION_NOT_EMPTY );
}

TEST_CASE( "Image surfaces copy logical rows and clear destination padding",
           "[CypherCommon][Image][Surface][Copy]" )
{
    std::array<byte, 64u> sourcePixels{};
    sourcePixels.fill( 0xEEu );

    // Every logical row contains twelve bytes; the remaining four source bytes
    // are padding and must not become part of the destination image.
    for ( u32 iSlice = 0u; iSlice < 2u; ++iSlice ) {
        for ( u32 iRow = 0u; iRow < 2u; ++iRow ) {
            const usize iOffset =
                static_cast<usize>( iSlice ) * 32u +
                static_cast<usize>( iRow ) * 16u;
            for ( usize iByte = 0u; iByte < 12u; ++iByte ) {
                sourcePixels[iOffset + iByte] =
                    static_cast<byte>( iOffset + iByte + 1u );
            }
        }
    }

    const const_image_view_t source{
        TestSurfaceDesc(),
        { sourcePixels.data(), sourcePixels.size() },
        16u,
        32u
    };
    image_surface_t surface{};
    REQUIRE( ImageSurface_CreateFromView(
                 &surface,
                 Allocator_GetSystem(),
                 source,
                 32u ) == image_surface_status_t::OK );
    REQUIRE( surface.layout.cbRowPitch == 32u );

    const image_surface_t &immutableSurface = surface;
    const const_image_view_t destination =
        ImageSurface_GetView( &immutableSurface );
    for ( u32 iSlice = 0u; iSlice < 2u; ++iSlice ) {
        for ( u32 iRow = 0u; iRow < 2u; ++iRow ) {
            const binary_block_t sourceRow =
                ImageView_GetRow( source, iRow, iSlice );
            const binary_block_t destinationRow =
                ImageView_GetRow( destination, iRow, iSlice );
            REQUIRE( Cy_MemEqual(
                sourceRow.pData,
                destinationRow.pData,
                12u ) );

            const usize iDestinationOffset =
                static_cast<usize>( iSlice ) * destination.cbSlicePitch +
                static_cast<usize>( iRow ) * destination.cbRowPitch;
            REQUIRE( Cy_MemIsZero(
                destination.pixels.pData + iDestinationOffset + 12u,
                20u ) );
        }
    }
}

TEST_CASE( "Image surfaces copy tightly packed pixels without changing data",
           "[CypherCommon][Image][Surface][Copy][Tight]" )
{
    std::array<byte, 64u> sourcePixels{};
    for ( usize iByte = 0u; iByte < sourcePixels.size(); ++iByte ) {
        sourcePixels[iByte] = static_cast<byte>( iByte + 1u );
    }

    const const_image_view_t source{
        {
            { 4u, 4u, 1u },
            image_pixel_format_t::RGBA8_UNORM,
            image_color_space_t::SRGB,
            image_alpha_mode_t::STRAIGHT
        },
        { sourcePixels.data(), sourcePixels.size() },
        16u,
        64u
    };
    image_surface_t surface{};
    REQUIRE( ImageSurface_CreateFromView(
                 &surface,
                 Allocator_GetSystem(),
                 source,
                 16u ) == image_surface_status_t::OK );

    const image_surface_t &immutableSurface = surface;
    const const_image_view_t destination =
        ImageSurface_GetView( &immutableSurface );
    REQUIRE( Cy_MemEqual(
        sourcePixels.data(),
        destination.pixels.pData,
        sourcePixels.size() ) );
}

TEST_CASE( "Image surfaces copy into existing storage without allocating",
           "[CypherCommon][Image][Surface][Copy][Reuse]" )
{
    allocator_probe_t probe{};
    const allocator_t allocator{
        ProbeAllocate,
        nullptr,
        ProbeFree,
        &probe
    };
    std::array<byte, 48u> sourcePixels{};
    for ( usize iByte = 0u; iByte < sourcePixels.size(); ++iByte ) {
        sourcePixels[iByte] = static_cast<byte>( iByte + 1u );
    }

    const const_image_view_t source{
        TestSurfaceDesc(),
        { sourcePixels.data(), sourcePixels.size() },
        12u,
        24u
    };
    image_surface_t surface{};
    REQUIRE( ImageSurface_Create(
                 &surface,
                 &allocator,
                 source.desc,
                 image_surface_init_t::UNINITIALIZED,
                 16u ) == image_surface_status_t::OK );
    Cy_MemSet(
        surface.allocation.pData,
        0xEEu,
        surface.allocation.cbSize );
    void *pOriginalPixels = surface.allocation.pData;

    REQUIRE( ImageSurface_CopyFromView( &surface, source ) ==
             image_surface_status_t::OK );
    REQUIRE( surface.allocation.pData == pOriginalPixels );
    REQUIRE( probe.cAllocations == 1u );
    REQUIRE( probe.cFrees == 0u );

    const image_surface_t &immutableSurface = surface;
    const const_image_view_t destination =
        ImageSurface_GetView( &immutableSurface );
    for ( u32 iSlice = 0u; iSlice < 2u; ++iSlice ) {
        for ( u32 iRow = 0u; iRow < 2u; ++iRow ) {
            const binary_block_t sourceRow =
                ImageView_GetRow( source, iRow, iSlice );
            const binary_block_t destinationRow =
                ImageView_GetRow( destination, iRow, iSlice );
            REQUIRE( Cy_MemEqual(
                sourceRow.pData,
                destinationRow.pData,
                sourceRow.cbSize ) );

            const usize iDestinationOffset =
                static_cast<usize>( iSlice ) * destination.cbSlicePitch +
                static_cast<usize>( iRow ) * destination.cbRowPitch;
            REQUIRE( Cy_MemIsZero(
                destination.pixels.pData + iDestinationOffset + 12u,
                4u ) );
        }
    }
}

TEST_CASE( "Image surface copy rejects incompatible and overlapping views",
           "[CypherCommon][Image][Surface][Copy][Failure]" )
{
    image_surface_t surface{};
    image_desc_t largeDesc = TestSurfaceDesc();
    largeDesc.extent = { 16u, 16u, 1u };
    REQUIRE( ImageSurface_Create(
                 &surface,
                 Allocator_GetSystem(),
                 largeDesc,
                 image_surface_init_t::ZEROED,
                 16u ) == image_surface_status_t::OK );

    image_desc_t activeDesc = largeDesc;
    activeDesc.extent = { 8u, 8u, 1u };
    REQUIRE( ImageSurface_Recreate(
                 &surface,
                 activeDesc,
                 image_surface_init_t::UNINITIALIZED,
                 16u ) == image_surface_status_t::OK );

    const image_surface_t &immutableSurface = surface;
    const const_image_view_t exactView =
        ImageSurface_GetView( &immutableSurface );
    REQUIRE( ImageSurface_CopyFromView( &surface, exactView ) ==
             image_surface_status_t::OK );

    const const_image_view_t overlappingView{
        surface.desc,
        {
            static_cast<const byte *>( surface.allocation.pData ) + 4u,
            surface.allocation.cbSize - 4u
        },
        surface.layout.cbRowPitch,
        surface.layout.cbSlicePitch
    };
    REQUIRE( ImageView_IsValid( overlappingView ) );
    REQUIRE( ImageSurface_CopyFromView( &surface, overlappingView ) ==
             image_surface_status_t::OVERLAPPING_SOURCE );

    std::array<byte, 64u> mismatchedPixels{};
    const const_image_view_t mismatchedView{
        {
            { 4u, 4u, 1u },
            image_pixel_format_t::RGBA8_UNORM,
            image_color_space_t::SRGB,
            image_alpha_mode_t::STRAIGHT
        },
        { mismatchedPixels.data(), mismatchedPixels.size() },
        16u,
        64u
    };
    REQUIRE( ImageSurface_CopyFromView( &surface, mismatchedView ) ==
             image_surface_status_t::DESCRIPTOR_MISMATCH );
}

TEST_CASE( "Image surface views borrow the owned pixels",
           "[CypherCommon][Image][Surface][View]" )
{
    image_surface_t surface{};
    REQUIRE( ImageSurface_GetView( &surface ).pixels.pData == nullptr );
    REQUIRE( ImageSurface_Create(
                 &surface,
                 Allocator_GetSystem(),
                 TestSurfaceDesc(),
                 image_surface_init_t::ZEROED ) ==
             image_surface_status_t::OK );

    image_view_t writable = ImageSurface_GetView( &surface );
    writable.pixels.pData[0] = 0xA5u;
    const image_surface_t &immutableSurface = surface;
    const const_image_view_t readable =
        ImageSurface_GetView( &immutableSurface );
    REQUIRE( readable.pixels.pData == surface.allocation.pData );
    REQUIRE( readable.pixels.pData[0] == 0xA5u );

    REQUIRE( ImageSurface_ZeroPixels( &surface ) );
    REQUIRE( readable.pixels.pData[0] == 0u );
}

TEST_CASE( "Image surface recreation is transactional",
           "[CypherCommon][Image][Surface][Recreate]" )
{
    allocator_t allocator = *Allocator_GetSystem();
    image_surface_t surface{};
    REQUIRE( ImageSurface_Recreate(
                 &surface,
                 TestSurfaceDesc(),
                 image_surface_init_t::ZEROED ) ==
             image_surface_status_t::INVALID_SURFACE_STATE );
    REQUIRE( ImageSurface_Create(
                 &surface,
                 &allocator,
                 TestSurfaceDesc(),
                 image_surface_init_t::ZEROED ) ==
             image_surface_status_t::OK );

    void *pOriginalPixels = surface.allocation.pData;
    static_cast<byte *>( pOriginalPixels )[0] = 0x7Bu;
    image_desc_t replacementDesc = TestSurfaceDesc();
    replacementDesc.extent = { 8u, 4u, 1u };

    allocator.pfnAllocate = FailAllocation;
    REQUIRE( ImageSurface_Recreate(
                 &surface,
                 replacementDesc,
                 image_surface_init_t::ZEROED,
                 16u ) == image_surface_status_t::ALLOCATION_FAILED );
    REQUIRE( surface.allocation.pData == pOriginalPixels );
    REQUIRE( static_cast<byte *>( surface.allocation.pData )[0] == 0x7Bu );
    REQUIRE( surface.desc.extent.nWidth == 3u );

    allocator.pfnAllocate = Allocator_GetSystem()->pfnAllocate;
    REQUIRE( ImageSurface_Recreate(
                 &surface,
                 replacementDesc,
                 image_surface_init_t::ZEROED,
                 16u ) == image_surface_status_t::OK );
    REQUIRE( ImageSurface_IsValid( &surface ) );
    REQUIRE( surface.desc.extent.nWidth == 8u );
    REQUIRE( surface.allocation.pData != pOriginalPixels );
}

TEST_CASE( "Image surface recreation reuses compatible capacity",
           "[CypherCommon][Image][Surface][Recreate][Reuse]" )
{
    allocator_probe_t probe{};
    const allocator_t allocator{
        ProbeAllocate,
        nullptr,
        ProbeFree,
        &probe
    };

    image_surface_t surface{};
    const image_desc_t largeDesc{
        { 64u, 64u, 1u },
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT
    };
    REQUIRE( ImageSurface_Create(
                 &surface,
                 &allocator,
                 largeDesc,
                 image_surface_init_t::UNINITIALIZED,
                 64u ) == image_surface_status_t::OK );
    void *pOriginalPixels = surface.allocation.pData;
    const usize cbOriginalCapacity = surface.allocation.cbSize;
    Cy_MemSet( pOriginalPixels, 0xA5u, cbOriginalCapacity );

    image_desc_t smallDesc = largeDesc;
    smallDesc.extent = { 8u, 8u, 1u };
    REQUIRE( ImageSurface_Recreate(
                 &surface,
                 smallDesc,
                 image_surface_init_t::ZEROED,
                 64u ) == image_surface_status_t::OK );
    REQUIRE( ImageSurface_IsValid( &surface ) );
    REQUIRE( surface.allocation.pData == pOriginalPixels );
    REQUIRE( ImageSurface_GetByteSize( &surface ) == 512u );
    REQUIRE( ImageSurface_GetCapacity( &surface ) == cbOriginalCapacity );
    REQUIRE( ImageSurface_GetView( &surface ).pixels.nCount == 512u );
    REQUIRE( Cy_MemIsZero( pOriginalPixels, 512u ) );
    REQUIRE( static_cast<const byte *>( pOriginalPixels )[512u] == 0xA5u );
    REQUIRE( probe.cAllocations == 1u );
    REQUIRE( probe.cFrees == 0u );

    // A block allocated at 64-byte alignment can support a tighter row layout
    // without changing the allocator metadata needed to free that block.
    REQUIRE( ImageSurface_Recreate(
                 &surface,
                 smallDesc,
                 image_surface_init_t::UNINITIALIZED,
                 16u ) == image_surface_status_t::OK );
    REQUIRE( surface.allocation.pData == pOriginalPixels );
    REQUIRE( surface.allocation.nAlignment == 64u );
    REQUIRE( surface.cbRowAlignment == 16u );
    REQUIRE( ImageSurface_GetByteSize( &surface ) == 256u );
    REQUIRE( probe.cAllocations == 1u );

    image_desc_t largerDesc = largeDesc;
    largerDesc.extent = { 128u, 128u, 1u };
    REQUIRE( ImageSurface_Recreate(
                 &surface,
                 largerDesc,
                 image_surface_init_t::UNINITIALIZED,
                 64u ) == image_surface_status_t::OK );
    REQUIRE( surface.allocation.pData != pOriginalPixels );
    REQUIRE( probe.cAllocations == 2u );
    REQUIRE( probe.cFrees == 1u );
}

TEST_CASE( "Image surfaces transfer and exchange ownership without copying",
           "[CypherCommon][Image][Surface][Ownership]" )
{
    image_surface_t source{};
    image_surface_t destination{};
    REQUIRE( ImageSurface_Create(
                 &source,
                 Allocator_GetSystem(),
                 TestSurfaceDesc(),
                 image_surface_init_t::ZEROED ) ==
             image_surface_status_t::OK );
    void *pSourcePixels = source.allocation.pData;

    REQUIRE( ImageSurface_Move( &destination, &source ) );
    REQUIRE( ImageSurface_IsEmpty( &source ) );
    REQUIRE( ImageSurface_IsValid( &destination ) );
    REQUIRE( destination.allocation.pData == pSourcePixels );

    image_surface_t other{};
    image_desc_t otherDesc = TestSurfaceDesc();
    otherDesc.extent = { 1u, 1u, 1u };
    REQUIRE( ImageSurface_Create(
                 &other,
                 Allocator_GetSystem(),
                 otherDesc,
                 image_surface_init_t::ZEROED ) ==
             image_surface_status_t::OK );
    void *pOtherPixels = other.allocation.pData;

    ImageSurface_Swap( &destination, &other );
    REQUIRE( destination.allocation.pData == pOtherPixels );
    REQUIRE( destination.desc.extent.nWidth == 1u );
    REQUIRE( other.allocation.pData == pSourcePixels );
    REQUIRE( other.desc.extent.nWidth == 3u );
    REQUIRE_FALSE( ImageSurface_Move( &destination, &other ) );
}

TEST_CASE( "Image surface validation detects inconsistent metadata",
           "[CypherCommon][Image][Surface][Validation]" )
{
    image_surface_t surface{};
    REQUIRE( ImageSurface_Create(
                 &surface,
                 Allocator_GetSystem(),
                 TestSurfaceDesc(),
                 image_surface_init_t::ZEROED,
                 16u ) == image_surface_status_t::OK );

    const usize cbOriginalRowPitch = surface.layout.cbRowPitch;
    ++surface.layout.cbRowPitch;
    REQUIRE_FALSE( ImageSurface_IsValid( &surface ) );
    surface.layout.cbRowPitch = cbOriginalRowPitch;
    REQUIRE( ImageSurface_IsValid( &surface ) );

    surface.cbRowAlignment = 4u;
    REQUIRE_FALSE( ImageSurface_IsValid( &surface ) );
    surface.cbRowAlignment = 16u;
    REQUIRE( ImageSurface_IsValid( &surface ) );
}

TEST_CASE( "Image surface destruction uses its originating allocator",
           "[CypherCommon][Image][Surface][Allocator]" )
{
    allocator_probe_t probe{};
    const allocator_t allocator{
        ProbeAllocate,
        nullptr,
        ProbeFree,
        &probe
    };

    {
        image_surface_t surface{};
        REQUIRE( ImageSurface_Create(
                     &surface,
                     &allocator,
                     TestSurfaceDesc(),
                     image_surface_init_t::UNINITIALIZED ) ==
                 image_surface_status_t::OK );
        REQUIRE( probe.cAllocations == 1u );
        REQUIRE( probe.cFrees == 0u );
    }

    REQUIRE( probe.cFrees == 1u );
}

TEST_CASE( "Image surface status names remain stable",
           "[CypherCommon][Image][Surface][Diagnostics]" )
{
    REQUIRE( std::string_view( ImageSurface_StatusName(
                 image_surface_status_t::ALLOCATION_FAILED ) ) ==
             "ALLOCATION_FAILED" );
    REQUIRE( std::string_view( ImageSurface_StatusName(
                 image_surface_status_t::INVALID_INITIALIZATION ) ) ==
             "INVALID_INITIALIZATION" );
    REQUIRE( std::string_view( ImageSurface_StatusName(
                 image_surface_status_t::DESCRIPTOR_MISMATCH ) ) ==
             "DESCRIPTOR_MISMATCH" );
    REQUIRE( std::string_view( ImageSurface_StatusName(
                 image_surface_status_t::OVERLAPPING_SOURCE ) ) ==
             "OVERLAPPING_SOURCE" );
    REQUIRE( std::string_view( ImageSurface_StatusName(
                 static_cast<image_surface_status_t>( 0xFFu ) ) ) ==
             "UNKNOWN_IMAGE_SURFACE_STATUS" );
}
