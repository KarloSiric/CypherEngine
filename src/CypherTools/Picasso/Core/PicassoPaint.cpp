//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Core/PicassoPaint.cpp
//  Purpose: Implements Picasso's allocation-conscious raster paint kernels.
//  Details: Brush loops walk pitched rows directly. Flood fill uses scanline
//           seeds so common regions do not require one queue entry per pixel.
//
//  History:
//  - Created by Karlo Siric on 2026-08-19
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoPaint.h"

#include "CypherCommon_ImageFormat.h"
#include "CypherCommon_MemoryOps.h"

#include <algorithm>
#include <cmath>

namespace cypher::tools::picasso
{

namespace
{

struct picasso_fill_seed_t {
    u32 x{ 0u };
    u32 y{ 0u };
};

bool_t PicassoPaint_IsRgba8Destination(
    const image_view_t &destination ) noexcept
{
    return ImageView_IsValid( destination ) &&
           destination.desc.extent.nDepth == 1u &&
           destination.desc.pixelFormat == image_pixel_format_t::RGBA8_UNORM;
}

bool_t PicassoPaint_IsDabValid( const picasso_brush_dab_t &dab ) noexcept
{
    return std::isfinite( dab.x ) && std::isfinite( dab.y ) &&
           std::isfinite( dab.nDiameter ) && dab.nDiameter > 0.0f &&
           std::isfinite( dab.opacity ) &&
           dab.opacity >= 0.0f && dab.opacity <= 1.0f &&
           std::isfinite( dab.hardness ) &&
           dab.hardness >= 0.0f && dab.hardness <= 1.0f &&
           ( dab.mode == picasso_brush_mode_t::PAINT ||
             dab.mode == picasso_brush_mode_t::ERASE );
}

byte PicassoPaint_ByteFromFloat( f32 value ) noexcept
{
    return static_cast<byte>( std::clamp(
        static_cast<int>( std::lround( value ) ),
        0,
        255 ) );
}

bool_t PicassoPaint_ApplyPixel(
    byte *pDestination,
    const picasso_brush_dab_t &dab,
    f32 coverage ) noexcept
{
    const byte before[4]{
        pDestination[0], pDestination[1],
        pDestination[2], pDestination[3]
    };

    if ( dab.mode == picasso_brush_mode_t::ERASE ) {
        pDestination[3] = PicassoPaint_ByteFromFloat(
            static_cast<f32>( pDestination[3] ) * ( 1.0f - coverage ) );
    } else {
        const f32 sourceAlpha =
            ( static_cast<f32>( dab.color[3] ) / 255.0f ) * coverage;
        const f32 destinationAlpha =
            static_cast<f32>( pDestination[3] ) / 255.0f;
        const f32 outputAlpha = sourceAlpha +
            destinationAlpha * ( 1.0f - sourceAlpha );

        for ( usize iComponent = 0u; iComponent < 3u; ++iComponent ) {
            const f32 source = static_cast<f32>( dab.color[iComponent] );
            const f32 destination =
                static_cast<f32>( pDestination[iComponent] );
            pDestination[iComponent] = outputAlpha > 0.0f
                ? PicassoPaint_ByteFromFloat(
                      ( source * sourceAlpha +
                        destination * destinationAlpha *
                            ( 1.0f - sourceAlpha ) ) / outputAlpha )
                : 0u;
        }
        pDestination[3] = PicassoPaint_ByteFromFloat( outputAlpha * 255.0f );
    }

    return !Cy_MemEqual( before, pDestination, sizeof( before ) );
}

bool_t PicassoPaint_PixelsEqual(
    const byte *pLeft,
    const byte *pRight ) noexcept
{
    return Cy_MemEqual( pLeft, pRight, 4u );
}

bool_t PicassoPaint_ReserveSeeds(
    const allocator_t *pAllocator,
    picasso_fill_seed_t **ppSeeds,
    usize &nCapacity,
    usize nRequired ) noexcept
{
    if ( nRequired <= nCapacity ) {
        return CY_TRUE;
    }
    usize nNewCapacity = nCapacity == 0u ? 64u : nCapacity;
    while ( nNewCapacity < nRequired ) {
        if ( nNewCapacity > CY_USIZE_MAX / 2u ) {
            nNewCapacity = nRequired;
            break;
        }
        nNewCapacity *= 2u;
    }

    picasso_fill_seed_t *pNewSeeds =
        Allocator_ReallocateArrayStorage(
            pAllocator,
            *ppSeeds,
            nCapacity,
            nNewCapacity );
    if ( pNewSeeds == nullptr ) {
        return CY_FALSE;
    }
    *ppSeeds = pNewSeeds;
    nCapacity = nNewCapacity;
    return CY_TRUE;
}

} // namespace

picasso_paint_status_t PicassoPaint_DabBounds(
    const image_view_t &destination,
    const picasso_brush_dab_t &dab,
    picasso_pixel_rect_t *pBoundsOut ) noexcept
{
    if ( pBoundsOut == nullptr || !PicassoPaint_IsDabValid( dab ) ) {
        return picasso_paint_status_t::INVALID_ARGUMENT;
    }
    *pBoundsOut = {};
    if ( !PicassoPaint_IsRgba8Destination( destination ) ) {
        return ImageView_IsValid( destination )
            ? picasso_paint_status_t::UNSUPPORTED_FORMAT
            : picasso_paint_status_t::INVALID_ARGUMENT;
    }

    const f32 radius = dab.nDiameter * 0.5f;
    const i64 xBegin = std::max<i64>(
        0,
        static_cast<i64>( std::floor( dab.x - radius ) ) );
    const i64 yBegin = std::max<i64>(
        0,
        static_cast<i64>( std::floor( dab.y - radius ) ) );
    const i64 xEnd = std::min<i64>(
        destination.desc.extent.nWidth,
        static_cast<i64>( std::ceil( dab.x + radius ) ) );
    const i64 yEnd = std::min<i64>(
        destination.desc.extent.nHeight,
        static_cast<i64>( std::ceil( dab.y + radius ) ) );
    if ( xBegin >= xEnd || yBegin >= yEnd || dab.opacity <= 0.0f ) {
        return picasso_paint_status_t::OK;
    }

    *pBoundsOut = {
        static_cast<u32>( xBegin ),
        static_cast<u32>( yBegin ),
        static_cast<u32>( xEnd - xBegin ),
        static_cast<u32>( yEnd - yBegin )
    };
    return picasso_paint_status_t::OK;
}

picasso_paint_status_t PicassoPaint_ApplyDab(
    const image_view_t &destination,
    const picasso_brush_dab_t &dab,
    bool_t *pChangedOut ) noexcept
{
    if ( pChangedOut == nullptr ) {
        return picasso_paint_status_t::INVALID_ARGUMENT;
    }
    *pChangedOut = CY_FALSE;

    picasso_pixel_rect_t bounds{};
    const picasso_paint_status_t boundsStatus = PicassoPaint_DabBounds(
        destination,
        dab,
        &bounds );
    if ( boundsStatus != picasso_paint_status_t::OK ||
         bounds.nWidth == 0u || bounds.nHeight == 0u ) {
        return boundsStatus;
    }

    const f32 radius = dab.nDiameter * 0.5f;
    const f32 radiusSquared = radius * radius;
    const f32 hardRadius = radius * dab.hardness;
    const f32 hardRadiusSquared = hardRadius * hardRadius;
    const f32 featherWidth = radius - hardRadius;

    for ( u32 y = bounds.y; y < bounds.y + bounds.nHeight; ++y ) {
        byte_span_t row = ImageView_GetRow( destination, y, 0u );
        for ( u32 x = bounds.x; x < bounds.x + bounds.nWidth; ++x ) {
            const f32 dx = static_cast<f32>( x ) + 0.5f - dab.x;
            const f32 dy = static_cast<f32>( y ) + 0.5f - dab.y;
            const f32 distanceSquared = dx * dx + dy * dy;
            if ( distanceSquared > radiusSquared ) {
                continue;
            }

            f32 falloff = 1.0f;
            if ( distanceSquared > hardRadiusSquared && featherWidth > 0.0f ) {
                const f32 distance = std::sqrt( distanceSquared );
                const f32 t = std::clamp(
                    ( distance - hardRadius ) / featherWidth,
                    0.0f,
                    1.0f );
                // Smoothstep avoids a visible ring where the hard core meets
                // the feathered edge while retaining deterministic output.
                falloff = 1.0f - t * t * ( 3.0f - 2.0f * t );
            }
            const f32 coverage = std::clamp(
                falloff * dab.opacity,
                0.0f,
                1.0f );
            if ( coverage <= 0.0f ) {
                continue;
            }
            *pChangedOut |= PicassoPaint_ApplyPixel(
                row.pData + static_cast<usize>( x ) * 4u,
                dab,
                coverage );
        }
    }
    return picasso_paint_status_t::OK;
}

picasso_paint_status_t PicassoPaint_FloodFill(
    const image_view_t &destination,
    u32 x,
    u32 y,
    const byte replacement[4],
    const allocator_t *pScratchAllocator,
    bool_t *pChangedOut ) noexcept
{
    if ( replacement == nullptr || pChangedOut == nullptr ||
         !Allocator_IsValid( pScratchAllocator ) ) {
        return picasso_paint_status_t::INVALID_ARGUMENT;
    }
    *pChangedOut = CY_FALSE;
    if ( !PicassoPaint_IsRgba8Destination( destination ) ) {
        return ImageView_IsValid( destination )
            ? picasso_paint_status_t::UNSUPPORTED_FORMAT
            : picasso_paint_status_t::INVALID_ARGUMENT;
    }
    if ( x >= destination.desc.extent.nWidth ||
         y >= destination.desc.extent.nHeight ) {
        return picasso_paint_status_t::INVALID_ARGUMENT;
    }

    const byte *pStart = ImageView_GetPixel( destination, x, y, 0u ).pData;
    byte target[4]{ pStart[0], pStart[1], pStart[2], pStart[3] };
    if ( PicassoPaint_PixelsEqual( target, replacement ) ) {
        return picasso_paint_status_t::OK;
    }

    picasso_fill_seed_t *pSeeds = nullptr;
    usize nSeedCount = 0u;
    usize nSeedCapacity = 0u;
    if ( !PicassoPaint_ReserveSeeds(
             pScratchAllocator,
             &pSeeds,
             nSeedCapacity,
             1u ) ) {
        return picasso_paint_status_t::ALLOCATION_FAILED;
    }
    pSeeds[nSeedCount++] = { x, y };

    const u32 nWidth = destination.desc.extent.nWidth;
    const u32 nHeight = destination.desc.extent.nHeight;
    picasso_paint_status_t status = picasso_paint_status_t::OK;
    while ( nSeedCount > 0u ) {
        const picasso_fill_seed_t seed = pSeeds[--nSeedCount];
        byte *pSeedPixel = ImageView_GetPixel(
            destination,
            seed.x,
            seed.y,
            0u ).pData;
        if ( !PicassoPaint_PixelsEqual( pSeedPixel, target ) ) {
            continue;
        }

        u32 xLeft = seed.x;
        u32 xRight = seed.x;
        while ( xLeft > 0u && PicassoPaint_PixelsEqual(
                    ImageView_GetPixel(
                        destination, xLeft - 1u, seed.y, 0u ).pData,
                    target ) ) {
            --xLeft;
        }
        while ( xRight + 1u < nWidth && PicassoPaint_PixelsEqual(
                    ImageView_GetPixel(
                        destination, xRight + 1u, seed.y, 0u ).pData,
                    target ) ) {
            ++xRight;
        }

        byte_span_t row = ImageView_GetRow( destination, seed.y, 0u );
        for ( u32 iColumn = xLeft; iColumn <= xRight; ++iColumn ) {
            Cy_MemCopy(
                row.pData + static_cast<usize>( iColumn ) * 4u,
                replacement,
                4u );
        }
        *pChangedOut = CY_TRUE;

        for ( i32 yOffset : { -1, 1 } ) {
            const i64 adjacentY = static_cast<i64>( seed.y ) + yOffset;
            if ( adjacentY < 0 || adjacentY >= nHeight ) {
                continue;
            }
            bool_t bInsideRun = CY_FALSE;
            for ( u32 iColumn = xLeft; iColumn <= xRight; ++iColumn ) {
                const bool_t bMatches = PicassoPaint_PixelsEqual(
                    ImageView_GetPixel(
                        destination,
                        iColumn,
                        static_cast<u32>( adjacentY ),
                        0u ).pData,
                    target );
                if ( bMatches && !bInsideRun ) {
                    if ( !PicassoPaint_ReserveSeeds(
                             pScratchAllocator,
                             &pSeeds,
                             nSeedCapacity,
                             nSeedCount + 1u ) ) {
                        status = picasso_paint_status_t::ALLOCATION_FAILED;
                        break;
                    }
                    pSeeds[nSeedCount++] = {
                        iColumn,
                        static_cast<u32>( adjacentY )
                    };
                }
                bInsideRun = bMatches;
            }
            if ( status != picasso_paint_status_t::OK ) {
                break;
            }
        }
        if ( status != picasso_paint_status_t::OK ) {
            break;
        }
    }

    Allocator_FreeArrayStorage(
        pScratchAllocator,
        pSeeds,
        nSeedCapacity );
    return status;
}

const char *PicassoPaint_StatusName( picasso_paint_status_t status ) noexcept
{
    switch ( status ) {
        case picasso_paint_status_t::OK:                 return "OK";
        case picasso_paint_status_t::INVALID_ARGUMENT:   return "INVALID_ARGUMENT";
        case picasso_paint_status_t::UNSUPPORTED_FORMAT: return "UNSUPPORTED_FORMAT";
        case picasso_paint_status_t::ALLOCATION_FAILED:  return "ALLOCATION_FAILED";
    }
    return "UNKNOWN";
}

} // namespace cypher::tools::picasso
