//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Core/PicassoTextureDocument.cpp
//  Purpose: Implements Picasso's Qt-independent texture document model.
//  Details: Document replacement is transactional, transform history stores
//           compact inverse commands, and imported files remain normalized to
//           the Common image subsystem's top-left, pitched-surface contract.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoTextureDocument.h"

#include "CypherCommon_ImageConvert.h"
#include "CypherCommon_ImageFormat.h"
#include "CypherCommon_ImageView.h"
#include "CypherCommon_MemoryOps.h"

#include <algorithm>

namespace cypher::tools::picasso
{

namespace
{

bool_t PicassoTextureDocument_IsInitialized(
    const picasso_texture_document_t *pDocument ) noexcept
{
    return pDocument != nullptr &&
           Allocator_IsValid( pDocument->pAllocator );
}

bool_t PicassoTextureDocument_IsOperationValid(
    picasso_texture_operation_t operation ) noexcept
{
    return operation == picasso_texture_operation_t::FLIP_HORIZONTAL ||
           operation == picasso_texture_operation_t::FLIP_VERTICAL ||
           operation == picasso_texture_operation_t::ROTATE_90_CLOCKWISE ||
           operation ==
               picasso_texture_operation_t::ROTATE_90_COUNTER_CLOCKWISE ||
           operation == picasso_texture_operation_t::ROTATE_180;
}

bool_t PicassoTextureDocument_IsCanvasValid(
    const picasso_canvas_desc_t &desc ) noexcept
{
    const bool_t bValidFill =
        desc.fill == picasso_canvas_fill_t::SOLID ||
        desc.fill == picasso_canvas_fill_t::CHECKERBOARD;
    return bValidFill &&
           desc.nWidth > 0u && desc.nHeight > 0u &&
           desc.nWidth <= PICASSO_TEXTURE_MAX_DIMENSION &&
           desc.nHeight <= PICASSO_TEXTURE_MAX_DIMENSION &&
           ( desc.fill != picasso_canvas_fill_t::CHECKERBOARD ||
             desc.nCheckerSize > 0u );
}

usize PicassoTextureDocument_PixelHistoryBytes(
    const picasso_pixel_history_t &pixels ) noexcept
{
    usize cbTiles = 0u;
    if ( !Cy_TryArrayByteCount<picasso_pixel_tile_patch_t>(
             pixels.nTileCapacity,
             cbTiles ) ||
         pixels.cbDataCapacity > CY_USIZE_MAX - cbTiles ) {
        return CY_USIZE_MAX;
    }
    return cbTiles + pixels.cbDataCapacity;
}

void PicassoTextureDocument_ReleasePixelHistory(
    const allocator_t *pAllocator,
    picasso_pixel_history_t &pixels ) noexcept
{
    if ( pixels.pTiles != nullptr ) {
        Allocator_FreeArrayStorage(
            pAllocator,
            pixels.pTiles,
            pixels.nTileCapacity );
    }
    if ( pixels.pData != nullptr ) {
        Allocator_Free(
            pAllocator,
            pixels.pData,
            pixels.cbDataCapacity );
    }
    pixels = {};
}

void PicassoTextureDocument_ReleaseHistoryEntry(
    picasso_texture_document_t &document,
    picasso_texture_history_entry_t &entry ) noexcept
{
    if ( entry.kind == picasso_history_kind_t::PIXEL_EDIT ) {
        const usize cbEntry =
            PicassoTextureDocument_PixelHistoryBytes( entry.pixels );
        PicassoTextureDocument_ReleasePixelHistory(
            document.pAllocator,
            entry.pixels );
        document.cbHistoryBytes = cbEntry <= document.cbHistoryBytes
            ? document.cbHistoryBytes - cbEntry
            : 0u;
    }
    entry = {};
}

void PicassoTextureDocument_ReleaseActiveEdit(
    picasso_texture_document_t &document ) noexcept
{
    picasso_active_pixel_edit_t &edit = document.activeEdit;
    if ( edit.pTiles != nullptr ) {
        Allocator_FreeArrayStorage(
            document.pAllocator,
            edit.pTiles,
            edit.nTileCapacity );
    }
    if ( edit.pData != nullptr ) {
        Allocator_Free(
            document.pAllocator,
            edit.pData,
            edit.cbDataCapacity );
    }
    if ( edit.pCapturedTiles != nullptr ) {
        Allocator_Free(
            document.pAllocator,
            edit.pCapturedTiles,
            edit.cbCapturedTiles );
    }
    edit = {};
}

void PicassoTextureDocument_ClearHistory(
    picasso_texture_document_t &document ) noexcept
{
    for ( usize iEntry = 0u;
          iEntry < document.nHistoryCount;
          ++iEntry ) {
        PicassoTextureDocument_ReleaseHistoryEntry(
            document,
            document.history[iEntry] );
    }
    document.nHistoryCount = 0u;
    document.iHistoryCursor = 0u;
    document.cbHistoryBytes = 0u;
    Cy_MemZero( document.history, sizeof( document.history ) );
}

bool_t PicassoTextureDocument_ReserveTilePatches(
    picasso_texture_document_t &document,
    usize nRequired ) noexcept
{
    picasso_active_pixel_edit_t &edit = document.activeEdit;
    if ( nRequired <= edit.nTileCapacity ) {
        return CY_TRUE;
    }
    usize nNewCapacity = edit.nTileCapacity == 0u
        ? 8u
        : edit.nTileCapacity;
    while ( nNewCapacity < nRequired ) {
        if ( nNewCapacity > CY_USIZE_MAX / 2u ) {
            nNewCapacity = nRequired;
            break;
        }
        nNewCapacity *= 2u;
    }
    picasso_pixel_tile_patch_t *pNewTiles =
        Allocator_ReallocateArrayStorage(
            document.pAllocator,
            edit.pTiles,
            edit.nTileCapacity,
            nNewCapacity );
    if ( pNewTiles == nullptr ) {
        return CY_FALSE;
    }
    edit.pTiles = pNewTiles;
    edit.nTileCapacity = nNewCapacity;
    return CY_TRUE;
}

bool_t PicassoTextureDocument_ReservePatchData(
    picasso_texture_document_t &document,
    usize cbRequired ) noexcept
{
    picasso_active_pixel_edit_t &edit = document.activeEdit;
    if ( cbRequired <= edit.cbDataCapacity ) {
        return CY_TRUE;
    }
    usize cbNewCapacity = edit.cbDataCapacity == 0u
        ? 64u * CY_KIB
        : edit.cbDataCapacity;
    while ( cbNewCapacity < cbRequired ) {
        const usize cbHalf = cbNewCapacity / 2u;
        if ( cbNewCapacity > CY_USIZE_MAX - cbHalf ) {
            cbNewCapacity = cbRequired;
            break;
        }
        cbNewCapacity += cbHalf;
    }
    byte *pNewData = static_cast<byte *>( Allocator_Reallocate(
        document.pAllocator,
        edit.pData,
        edit.cbDataCapacity,
        cbNewCapacity ) );
    if ( pNewData == nullptr ) {
        return CY_FALSE;
    }
    edit.pData = pNewData;
    edit.cbDataCapacity = cbNewCapacity;
    return CY_TRUE;
}

bool_t PicassoTextureDocument_IsTileCaptured(
    const picasso_active_pixel_edit_t &edit,
    usize iTile ) noexcept
{
    return ( edit.pCapturedTiles[iTile / 8u] &
             static_cast<byte>( 1u << ( iTile % 8u ) ) ) != 0u;
}

void PicassoTextureDocument_MarkTileCaptured(
    picasso_active_pixel_edit_t &edit,
    usize iTile ) noexcept
{
    edit.pCapturedTiles[iTile / 8u] |=
        static_cast<byte>( 1u << ( iTile % 8u ) );
}

bool_t PicassoTextureDocument_CaptureTile(
    picasso_texture_document_t &document,
    image_surface_t &surface,
    u32 iTileX,
    u32 iTileY ) noexcept
{
    picasso_active_pixel_edit_t &edit = document.activeEdit;
    const usize iTile = static_cast<usize>( iTileY ) *
                            edit.nTilesPerRow +
                        iTileX;
    if ( PicassoTextureDocument_IsTileCaptured( edit, iTile ) ) {
        return CY_TRUE;
    }

    const image_format_info_t *pFormat = ImageFormat_GetInfo(
        surface.desc.pixelFormat );
    if ( pFormat == nullptr || pFormat->cbPixel == 0u ) {
        return CY_FALSE;
    }
    const u32 x = iTileX * PICASSO_TEXTURE_HISTORY_TILE_SIZE;
    const u32 y = iTileY * PICASSO_TEXTURE_HISTORY_TILE_SIZE;
    const u32 nWidth = std::min(
        PICASSO_TEXTURE_HISTORY_TILE_SIZE,
        surface.desc.extent.nWidth - x );
    const u32 nHeight = std::min(
        PICASSO_TEXTURE_HISTORY_TILE_SIZE,
        surface.desc.extent.nHeight - y );
    const usize cbRow = static_cast<usize>( nWidth ) * pFormat->cbPixel;
    if ( nHeight > 0u && cbRow > CY_USIZE_MAX / nHeight ) {
        return CY_FALSE;
    }
    const usize cbTile = cbRow * nHeight;
    if ( cbTile > CY_USIZE_MAX - edit.cbData ||
         !PicassoTextureDocument_ReserveTilePatches(
             document,
             edit.nTileCount + 1u ) ||
         !PicassoTextureDocument_ReservePatchData(
             document,
             edit.cbData + cbTile ) ) {
        return CY_FALSE;
    }

    const image_view_t view = ImageSurface_GetView( &surface );
    const usize iDataOffset = edit.cbData;
    for ( u32 iRow = 0u; iRow < nHeight; ++iRow ) {
        const byte_span_t sourceRow = ImageView_GetRow(
            view,
            y + iRow,
            0u );
        Cy_MemCopy(
            edit.pData + iDataOffset +
                static_cast<usize>( iRow ) * cbRow,
            sourceRow.pData + static_cast<usize>( x ) * pFormat->cbPixel,
            cbRow );
    }
    edit.pTiles[edit.nTileCount++] = {
        x,
        y,
        nWidth,
        nHeight,
        cbRow,
        iDataOffset
    };
    edit.cbData += cbTile;
    PicassoTextureDocument_MarkTileCaptured( edit, iTile );
    return CY_TRUE;
}

bool_t PicassoTextureDocument_CaptureRect(
    picasso_texture_document_t &document,
    image_surface_t &surface,
    const picasso_pixel_rect_t &rect ) noexcept
{
    if ( rect.nWidth == 0u || rect.nHeight == 0u ) {
        return CY_TRUE;
    }
    const u32 iTileXBegin = rect.x / PICASSO_TEXTURE_HISTORY_TILE_SIZE;
    const u32 iTileYBegin = rect.y / PICASSO_TEXTURE_HISTORY_TILE_SIZE;
    const u32 iTileXEnd =
        ( rect.x + rect.nWidth - 1u ) /
        PICASSO_TEXTURE_HISTORY_TILE_SIZE;
    const u32 iTileYEnd =
        ( rect.y + rect.nHeight - 1u ) /
        PICASSO_TEXTURE_HISTORY_TILE_SIZE;
    for ( u32 iTileY = iTileYBegin; iTileY <= iTileYEnd; ++iTileY ) {
        for ( u32 iTileX = iTileXBegin; iTileX <= iTileXEnd; ++iTileX ) {
            if ( !PicassoTextureDocument_CaptureTile(
                     document,
                     surface,
                     iTileX,
                     iTileY ) ) {
                return CY_FALSE;
            }
        }
    }
    return CY_TRUE;
}

bool_t PicassoTextureDocument_SwapPixelPatches(
    image_surface_t &surface,
    const picasso_pixel_tile_patch_t *pTiles,
    usize nTileCount,
    byte *pData ) noexcept
{
    const image_format_info_t *pFormat = ImageFormat_GetInfo(
        surface.desc.pixelFormat );
    if ( pFormat == nullptr || pFormat->cbPixel == 0u ||
         pTiles == nullptr || pData == nullptr ) {
        return CY_FALSE;
    }
    const image_view_t view = ImageSurface_GetView( &surface );
    for ( usize iTile = 0u; iTile < nTileCount; ++iTile ) {
        const picasso_pixel_tile_patch_t &tile = pTiles[iTile];
        for ( u32 iRow = 0u; iRow < tile.nHeight; ++iRow ) {
            byte_span_t destinationRow = ImageView_GetRow(
                view,
                tile.y + iRow,
                0u );
            byte *pDestination = destinationRow.pData +
                static_cast<usize>( tile.x ) * pFormat->cbPixel;
            byte *pPatch = pData + tile.iDataOffset +
                static_cast<usize>( iRow ) * tile.cbRow;
            for ( usize iByte = 0u; iByte < tile.cbRow; ++iByte ) {
                const byte temporary = pDestination[iByte];
                pDestination[iByte] = pPatch[iByte];
                pPatch[iByte] = temporary;
            }
        }
    }
    return CY_TRUE;
}

u64 PicassoTextureDocument_AllocateState(
    picasso_texture_document_t &document ) noexcept
{
    if ( document.nNextState == 0u ||
         document.nNextState == CY_U64_MAX ) {
        // State identifiers are local to one editing session. Rebase only after
        // an unreachable number of edits and invalidate an old saved marker.
        PicassoTextureDocument_ClearHistory( document );
        document.nCurrentState = 0u;
        document.nSavedState = 0u;
        document.nNextState = 1u;
    }
    return document.nNextState++;
}

void PicassoTextureDocument_PublishTextureSet(
    picasso_texture_document_t &document,
    picasso_texture_set_t &pending,
    image_file_format_t sourceFormat,
    bool_t bInitiallySaved ) noexcept
{
    PicassoTextureDocument_ReleaseActiveEdit( document );
    PicassoTextureDocument_ClearHistory( document );
    (void) PicassoTextureSet_Swap( &document.textureSet, &pending );
    PicassoTextureSet_Shutdown( &pending );
    document.sourceFormat = sourceFormat;
    document.nCurrentState = PicassoTextureDocument_AllocateState( document );
    document.nSavedState = bInitiallySaved
        ? document.nCurrentState
        : 0u;
}

image_process_status_t PicassoTextureDocument_TransformSurfaceInPlace(
    image_surface_t &surface,
    picasso_texture_operation_t operation ) noexcept
{
    const image_view_t destination = ImageSurface_GetView( &surface );
    const const_image_view_t source = ImageView_AsConst( destination );
    switch ( operation ) {
        case picasso_texture_operation_t::FLIP_HORIZONTAL:
            return ImageProcess_FlipHorizontal( destination, source );
        case picasso_texture_operation_t::FLIP_VERTICAL:
            return ImageProcess_FlipVertical( destination, source );
        case picasso_texture_operation_t::ROTATE_180:
            return ImageProcess_Rotate180( destination, source );
        default:
            return image_process_status_t::IN_PLACE_NOT_SUPPORTED;
    }
}

picasso_document_status_t PicassoTextureDocument_RotateTextureSet90(
    picasso_texture_document_t &document,
    picasso_texture_operation_t operation ) noexcept
{
    picasso_texture_set_t pending{};
    if ( PicassoTextureSet_Init( &pending, document.pAllocator ) !=
             picasso_texture_set_status_t::OK ||
         PicassoTextureSet_Create(
             &pending,
             document.textureSet.extent.nHeight,
             document.textureSet.extent.nWidth ) !=
             picasso_texture_set_status_t::OK ) {
        return picasso_document_status_t::ALLOCATION_FAILED;
    }

    for ( usize iChannel = 0u;
          iChannel < PICASSO_CHANNEL_COUNT;
          ++iChannel ) {
        const auto semantic =
            static_cast<picasso_channel_semantic_t>( iChannel );
        const image_surface_t *pSource = PicassoTextureSet_GetChannel(
            &document.textureSet,
            semantic );
        if ( pSource == nullptr ) {
            continue;
        }

        image_desc_t rotatedDesc = pSource->desc;
        rotatedDesc.extent.nWidth = pSource->desc.extent.nHeight;
        rotatedDesc.extent.nHeight = pSource->desc.extent.nWidth;
        image_surface_t rotated{};
        if ( ImageSurface_Create(
                 &rotated,
                 document.pAllocator,
                 rotatedDesc,
                 image_surface_init_t::UNINITIALIZED,
                 pSource->cbRowAlignment ) != image_surface_status_t::OK ) {
            PicassoTextureSet_Shutdown( &pending );
            return picasso_document_status_t::ALLOCATION_FAILED;
        }

        const image_process_status_t processStatus = operation ==
                picasso_texture_operation_t::ROTATE_90_CLOCKWISE
            ? ImageProcess_Rotate90Clockwise(
                  ImageSurface_GetView( &rotated ),
                  ImageSurface_GetView( pSource ) )
            : ImageProcess_Rotate90CounterClockwise(
                  ImageSurface_GetView( &rotated ),
                  ImageSurface_GetView( pSource ) );
        if ( processStatus != image_process_status_t::OK ||
             PicassoTextureSet_AdoptChannelSurface(
                 &pending,
                 semantic,
                 &rotated ) != picasso_texture_set_status_t::OK ) {
            PicassoTextureSet_Shutdown( &pending );
            return picasso_document_status_t::PROCESSING_FAILED;
        }
    }

    (void) PicassoTextureSet_Swap( &document.textureSet, &pending );
    PicassoTextureSet_Shutdown( &pending );
    return picasso_document_status_t::OK;
}

picasso_texture_operation_t PicassoTextureDocument_Inverse(
    picasso_texture_operation_t operation ) noexcept
{
    switch ( operation ) {
        case picasso_texture_operation_t::ROTATE_90_CLOCKWISE:
            return picasso_texture_operation_t::ROTATE_90_COUNTER_CLOCKWISE;
        case picasso_texture_operation_t::ROTATE_90_COUNTER_CLOCKWISE:
            return picasso_texture_operation_t::ROTATE_90_CLOCKWISE;
        default:
            return operation;
    }
}

picasso_document_status_t PicassoTextureDocument_Execute(
    picasso_texture_document_t &document,
    picasso_texture_operation_t operation ) noexcept
{
    if ( !PicassoTextureDocument_IsOperationValid( operation ) ) {
        return picasso_document_status_t::INVALID_ARGUMENT;
    }

    if ( !PicassoTextureSet_IsValid( &document.textureSet ) ) {
        return picasso_document_status_t::PROCESSING_FAILED;
    }

    if ( operation == picasso_texture_operation_t::ROTATE_90_CLOCKWISE ||
         operation ==
             picasso_texture_operation_t::ROTATE_90_COUNTER_CLOCKWISE ) {
        return PicassoTextureDocument_RotateTextureSet90(
            document,
            operation );
    }

    // In-place flip and 180-degree kernels cannot fail after the complete set
    // has been validated, so mutating every active channel remains atomic from
    // the caller's perspective without cloning a potentially large texture set.
    for ( usize iChannel = 0u;
          iChannel < PICASSO_CHANNEL_COUNT;
          ++iChannel ) {
        const auto semantic =
            static_cast<picasso_channel_semantic_t>( iChannel );
        image_surface_t *pSurface = PicassoTextureSet_GetChannel(
            &document.textureSet,
            semantic );
        if ( pSurface == nullptr ) {
            continue;
        }
        if ( PicassoTextureDocument_TransformSurfaceInPlace(
                 *pSurface,
                 operation ) != image_process_status_t::OK ||
             PicassoTextureSet_MarkChannelChanged(
                 &document.textureSet,
                 semantic ) != picasso_texture_set_status_t::OK ) {
            return picasso_document_status_t::PROCESSING_FAILED;
        }
    }
    return picasso_document_status_t::OK;
}

void PicassoTextureDocument_DiscardRedoHistory(
    picasso_texture_document_t &document ) noexcept
{
    for ( usize iEntry = document.iHistoryCursor;
          iEntry < document.nHistoryCount;
          ++iEntry ) {
        PicassoTextureDocument_ReleaseHistoryEntry(
            document,
            document.history[iEntry] );
    }
    document.nHistoryCount = document.iHistoryCursor;
}

void PicassoTextureDocument_EvictOldestHistory(
    picasso_texture_document_t &document ) noexcept
{
    if ( document.nHistoryCount == 0u ) {
        return;
    }
    PicassoTextureDocument_ReleaseHistoryEntry(
        document,
        document.history[0] );
    if ( document.nHistoryCount > 1u ) {
        Cy_MemMove(
            document.history,
            document.history + 1u,
            ( document.nHistoryCount - 1u ) *
                sizeof( picasso_texture_history_entry_t ) );
    }
    --document.nHistoryCount;
    if ( document.iHistoryCursor > 0u ) {
        --document.iHistoryCursor;
    }
    document.history[document.nHistoryCount] = {};
}

void PicassoTextureDocument_PrepareHistorySlot(
    picasso_texture_document_t &document ) noexcept
{
    PicassoTextureDocument_DiscardRedoHistory( document );
    if ( document.nHistoryCount == PICASSO_TEXTURE_HISTORY_CAPACITY ) {
        PicassoTextureDocument_EvictOldestHistory( document );
    }
}

void PicassoTextureDocument_TrimHistoryBudget(
    picasso_texture_document_t &document ) noexcept
{
    // Keep at least the newest edit even when one full-canvas operation is
    // larger than the preferred budget; an undo button that immediately loses
    // its latest operation is more surprising than a temporary budget overrun.
    while ( document.nHistoryCount > 1u &&
            document.cbHistoryBytes >
                PICASSO_TEXTURE_HISTORY_BYTE_BUDGET ) {
        PicassoTextureDocument_EvictOldestHistory( document );
    }
}

void PicassoTextureDocument_PushTransformHistory(
    picasso_texture_document_t &document,
    picasso_texture_operation_t operation,
    u64 nBeforeState,
    u64 nAfterState ) noexcept
{
    PicassoTextureDocument_PrepareHistorySlot( document );
    document.history[document.nHistoryCount++] = {
        picasso_history_kind_t::TRANSFORM,
        operation,
        {},
        nBeforeState,
        nAfterState
    };
    document.iHistoryCursor = document.nHistoryCount;
}

void PicassoTextureDocument_PushPixelHistory(
    picasso_texture_document_t &document,
    u64 nBeforeState,
    u64 nAfterState ) noexcept
{
    PicassoTextureDocument_PrepareHistorySlot( document );
    picasso_active_pixel_edit_t &edit = document.activeEdit;
    picasso_texture_history_entry_t &entry =
        document.history[document.nHistoryCount++];
    entry.kind = picasso_history_kind_t::PIXEL_EDIT;
    entry.pixels.semantic = edit.semantic;
    entry.pixels.editKind = edit.editKind;
    entry.pixels.pTiles = edit.pTiles;
    entry.pixels.nTileCount = edit.nTileCount;
    entry.pixels.nTileCapacity = edit.nTileCapacity;
    entry.pixels.pData = edit.pData;
    entry.pixels.cbData = edit.cbData;
    entry.pixels.cbDataCapacity = edit.cbDataCapacity;
    entry.nBeforeState = nBeforeState;
    entry.nAfterState = nAfterState;

    const usize cbEntry =
        PicassoTextureDocument_PixelHistoryBytes( entry.pixels );
    document.cbHistoryBytes = cbEntry <=
            CY_USIZE_MAX - document.cbHistoryBytes
        ? document.cbHistoryBytes + cbEntry
        : CY_USIZE_MAX;
    document.iHistoryCursor = document.nHistoryCount;

    edit.pTiles = nullptr;
    edit.nTileCount = 0u;
    edit.nTileCapacity = 0u;
    edit.pData = nullptr;
    edit.cbData = 0u;
    edit.cbDataCapacity = 0u;
    PicassoTextureDocument_TrimHistoryBudget( document );
}

picasso_document_status_t PicassoTextureDocument_FromPaintStatus(
    picasso_paint_status_t status ) noexcept
{
    switch ( status ) {
        case picasso_paint_status_t::OK:
            return picasso_document_status_t::OK;
        case picasso_paint_status_t::INVALID_ARGUMENT:
            return picasso_document_status_t::INVALID_ARGUMENT;
        case picasso_paint_status_t::UNSUPPORTED_FORMAT:
            return picasso_document_status_t::UNSUPPORTED_SURFACE;
        case picasso_paint_status_t::ALLOCATION_FAILED:
            return picasso_document_status_t::ALLOCATION_FAILED;
    }
    return picasso_document_status_t::PROCESSING_FAILED;
}

bool_t PicassoTextureDocument_IsPixelEditKindValid(
    picasso_pixel_edit_kind_t editKind ) noexcept
{
    return editKind == picasso_pixel_edit_kind_t::BRUSH_STROKE ||
           editKind == picasso_pixel_edit_kind_t::ERASER_STROKE ||
           editKind == picasso_pixel_edit_kind_t::FLOOD_FILL;
}

picasso_document_status_t PicassoTextureDocument_ExecuteHistoryEntry(
    picasso_texture_document_t &document,
    const picasso_texture_history_entry_t &entry,
    bool_t bUndo ) noexcept
{
    if ( entry.kind == picasso_history_kind_t::TRANSFORM ) {
        return PicassoTextureDocument_Execute(
            document,
            bUndo
                ? PicassoTextureDocument_Inverse( entry.operation )
                : entry.operation );
    }
    if ( entry.kind != picasso_history_kind_t::PIXEL_EDIT ) {
        return picasso_document_status_t::PROCESSING_FAILED;
    }

    image_surface_t *pSurface = PicassoTextureSet_GetChannel(
        &document.textureSet,
        entry.pixels.semantic );
    if ( pSurface == nullptr ||
         !PicassoTextureDocument_SwapPixelPatches(
             *pSurface,
             entry.pixels.pTiles,
             entry.pixels.nTileCount,
             entry.pixels.pData ) ) {
        return picasso_document_status_t::PROCESSING_FAILED;
    }
    return PicassoTextureSet_MarkChannelChanged(
               &document.textureSet,
               entry.pixels.semantic ) == picasso_texture_set_status_t::OK
        ? picasso_document_status_t::OK
        : picasso_document_status_t::PROCESSING_FAILED;
}

} // namespace

picasso_document_status_t PicassoTextureDocument_Init(
    picasso_texture_document_t *pDocument,
    const allocator_t *pAllocator ) noexcept
{
    if ( pDocument == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return picasso_document_status_t::INVALID_ARGUMENT;
    }
    if ( pDocument->pAllocator != nullptr ||
         pDocument->textureSet.pAllocator != nullptr ) {
        return picasso_document_status_t::INVALID_ARGUMENT;
    }
    if ( PicassoTextureSet_Init(
             &pDocument->textureSet,
             pAllocator ) != picasso_texture_set_status_t::OK ) {
        return picasso_document_status_t::INVALID_ARGUMENT;
    }
    pDocument->pAllocator = pAllocator;
    return picasso_document_status_t::OK;
}

void PicassoTextureDocument_Shutdown(
    picasso_texture_document_t *pDocument ) noexcept
{
    if ( pDocument == nullptr ) {
        return;
    }
    PicassoTextureDocument_ReleaseActiveEdit( *pDocument );
    PicassoTextureDocument_ClearHistory( *pDocument );
    PicassoTextureSet_Shutdown( &pDocument->textureSet );
    pDocument->pAllocator = nullptr;
    pDocument->sourceFormat = image_file_format_t::UNKNOWN;
    pDocument->nCurrentState = 0u;
    pDocument->nSavedState = 0u;
    pDocument->nNextState = 1u;
}

picasso_document_status_t PicassoTextureDocument_Create(
    picasso_texture_document_t *pDocument,
    const picasso_canvas_desc_t &desc ) noexcept
{
    if ( !PicassoTextureDocument_IsInitialized( pDocument ) ) {
        return pDocument == nullptr
            ? picasso_document_status_t::INVALID_ARGUMENT
            : picasso_document_status_t::NOT_INITIALIZED;
    }
    if ( !PicassoTextureDocument_IsCanvasValid( desc ) ) {
        return picasso_document_status_t::INVALID_CANVAS;
    }

    picasso_texture_set_t pending{};
    const image_desc_t imageDesc{
        { desc.nWidth, desc.nHeight, 1u },
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT
    };
    const image_layout_result_t layout = ImageFormat_CalculateLayout(
        imageDesc,
        64u );
    if ( layout.status != image_format_status_t::OK ||
         layout.layout.cbTotalSize > PICASSO_TEXTURE_MAX_DECODED_SIZE ) {
        return picasso_document_status_t::INVALID_CANVAS;
    }
    if ( PicassoTextureSet_Init( &pending, pDocument->pAllocator ) !=
             picasso_texture_set_status_t::OK ||
         PicassoTextureSet_Create(
             &pending,
             desc.nWidth,
             desc.nHeight ) != picasso_texture_set_status_t::OK ||
         PicassoTextureSet_AddChannel(
             &pending,
             PicassoChannel_DefaultDesc(
                 picasso_channel_semantic_t::BASE_COLOR ),
             PicassoChannel_DefaultValue(
                 picasso_channel_semantic_t::BASE_COLOR ) ) !=
             picasso_texture_set_status_t::OK ) {
        PicassoTextureSet_Shutdown( &pending );
        return picasso_document_status_t::ALLOCATION_FAILED;
    }

    image_surface_t *pBaseColor = PicassoTextureSet_GetChannel(
        &pending,
        picasso_channel_semantic_t::BASE_COLOR );
    const image_view_t view = ImageSurface_GetView( pBaseColor );
    for ( u32 iRow = 0u; iRow < desc.nHeight; ++iRow ) {
        byte_span_t row = ImageView_GetRow( view, iRow, 0u );
        for ( u32 iColumn = 0u; iColumn < desc.nWidth; ++iColumn ) {
            const bool_t bUseSecond =
                desc.fill == picasso_canvas_fill_t::CHECKERBOARD &&
                ( ( iColumn / desc.nCheckerSize ) +
                  ( iRow / desc.nCheckerSize ) ) % 2u != 0u;
            const byte *pColor = bUseSecond ? desc.colorB : desc.colorA;
            Cy_MemCopy(
                row.pData + static_cast<usize>( iColumn ) * 4u,
                pColor,
                4u );
        }
    }

    (void) PicassoTextureSet_MarkChannelChanged(
        &pending,
        picasso_channel_semantic_t::BASE_COLOR );
    PicassoTextureDocument_PublishTextureSet(
        *pDocument,
        pending,
        image_file_format_t::UNKNOWN,
        CY_FALSE );
    return picasso_document_status_t::OK;
}

picasso_document_status_t PicassoTextureDocument_OpenEncoded(
    picasso_texture_document_t *pDocument,
    binary_block_t encoded,
    string_view_t sourcePath ) noexcept
{
    if ( !PicassoTextureDocument_IsInitialized( pDocument ) ) {
        return pDocument == nullptr
            ? picasso_document_status_t::INVALID_ARGUMENT
            : picasso_document_status_t::NOT_INITIALIZED;
    }

    image_decode_options_t options{};
    options.formatHint = ImageCodec_FormatFromPath( sourcePath );
    options.nMaximumDimension = PICASSO_TEXTURE_MAX_DIMENSION;
    options.cbMaximumDecodedSize = PICASSO_TEXTURE_MAX_DECODED_SIZE;
    options.cbRowAlignment = 64u;
    image_surface_t decoded{};
    const image_decode_result_t result = ImageCodec_Decode(
        encoded,
        pDocument->pAllocator,
        options,
        &decoded );
    if ( result.status != image_codec_status_t::OK ) {
        return picasso_document_status_t::DECODE_FAILED;
    }

    picasso_texture_set_t pending{};
    if ( PicassoTextureSet_Init( &pending, pDocument->pAllocator ) !=
             picasso_texture_set_status_t::OK ||
         PicassoTextureSet_Create(
             &pending,
             decoded.desc.extent.nWidth,
             decoded.desc.extent.nHeight ) !=
             picasso_texture_set_status_t::OK ||
         PicassoTextureSet_AdoptChannelSurface(
             &pending,
             picasso_channel_semantic_t::BASE_COLOR,
             &decoded ) != picasso_texture_set_status_t::OK ) {
        PicassoTextureSet_Shutdown( &pending );
        return picasso_document_status_t::PROCESSING_FAILED;
    }

    PicassoTextureDocument_PublishTextureSet(
        *pDocument,
        pending,
        result.sourceFormat,
        CY_TRUE );
    return picasso_document_status_t::OK;
}

picasso_document_status_t PicassoTextureDocument_ExportPng(
    const picasso_texture_document_t *pDocument,
    blob_t *pEncodedOut ) noexcept
{
    if ( !PicassoTextureDocument_IsInitialized( pDocument ) ||
         pEncodedOut == nullptr ) {
        return pDocument == nullptr || pEncodedOut == nullptr
            ? picasso_document_status_t::INVALID_ARGUMENT
            : picasso_document_status_t::NOT_INITIALIZED;
    }
    if ( !PicassoTextureDocument_IsOpen( pDocument ) ) {
        return picasso_document_status_t::NO_DOCUMENT;
    }

    const const_image_view_t source = PicassoTextureDocument_View( pDocument );
    if ( source.desc.pixelFormat == image_pixel_format_t::RGBA8_UNORM ) {
        return ImageCodec_EncodePng(
                   source,
                   pDocument->pAllocator,
                   pEncodedOut ) == image_codec_status_t::OK
            ? picasso_document_status_t::OK
            : picasso_document_status_t::ENCODE_FAILED;
    }

    image_desc_t convertedDesc = source.desc;
    convertedDesc.pixelFormat = image_pixel_format_t::RGBA8_UNORM;
    convertedDesc.colorSpace = image_color_space_t::SRGB;
    image_surface_t converted{};
    if ( ImageSurface_Create(
             &converted,
             pDocument->pAllocator,
             convertedDesc,
             image_surface_init_t::UNINITIALIZED,
             1u ) != image_surface_status_t::OK ) {
        return picasso_document_status_t::ALLOCATION_FAILED;
    }
    if ( ImageConvert(
             ImageSurface_GetView( &converted ),
             source ) != image_convert_status_t::OK ) {
        return picasso_document_status_t::PROCESSING_FAILED;
    }
    return ImageCodec_EncodePng(
               ImageSurface_GetView(
                   static_cast<const image_surface_t *>( &converted ) ),
               pDocument->pAllocator,
               pEncodedOut ) == image_codec_status_t::OK
        ? picasso_document_status_t::OK
        : picasso_document_status_t::ENCODE_FAILED;
}

picasso_document_status_t PicassoTextureDocument_Apply(
    picasso_texture_document_t *pDocument,
    picasso_texture_operation_t operation ) noexcept
{
    if ( !PicassoTextureDocument_IsInitialized( pDocument ) ||
         !PicassoTextureDocument_IsOperationValid( operation ) ) {
        return pDocument == nullptr ||
               !PicassoTextureDocument_IsOperationValid( operation )
            ? picasso_document_status_t::INVALID_ARGUMENT
            : picasso_document_status_t::NOT_INITIALIZED;
    }
    if ( !PicassoTextureDocument_IsOpen( pDocument ) ) {
        return picasso_document_status_t::NO_DOCUMENT;
    }
    if ( pDocument->activeEdit.bActive ) {
        return picasso_document_status_t::EDIT_ALREADY_ACTIVE;
    }

    const u64 nBeforeState = pDocument->nCurrentState;
    const picasso_document_status_t status =
        PicassoTextureDocument_Execute( *pDocument, operation );
    if ( status != picasso_document_status_t::OK ) {
        return status;
    }
    const u64 nAfterState = PicassoTextureDocument_AllocateState( *pDocument );
    pDocument->nCurrentState = nAfterState;
    PicassoTextureDocument_PushTransformHistory(
        *pDocument,
        operation,
        nBeforeState,
        nAfterState );
    return picasso_document_status_t::OK;
}

picasso_document_status_t PicassoTextureDocument_BeginPixelEdit(
    picasso_texture_document_t *pDocument,
    picasso_channel_semantic_t semantic,
    picasso_pixel_edit_kind_t editKind ) noexcept
{
    if ( !PicassoTextureDocument_IsInitialized( pDocument ) ||
         !PicassoChannel_IsSemanticValid( semantic ) ||
         !PicassoTextureDocument_IsPixelEditKindValid( editKind ) ) {
        return pDocument == nullptr ||
               !PicassoChannel_IsSemanticValid( semantic ) ||
               !PicassoTextureDocument_IsPixelEditKindValid( editKind )
            ? picasso_document_status_t::INVALID_ARGUMENT
            : picasso_document_status_t::NOT_INITIALIZED;
    }
    if ( !PicassoTextureDocument_IsOpen( pDocument ) ) {
        return picasso_document_status_t::NO_DOCUMENT;
    }
    if ( pDocument->activeEdit.bActive ) {
        return picasso_document_status_t::EDIT_ALREADY_ACTIVE;
    }

    image_surface_t *pSurface = PicassoTextureSet_GetChannel(
        &pDocument->textureSet,
        semantic );
    if ( pSurface == nullptr ) {
        return picasso_document_status_t::INVALID_ARGUMENT;
    }
    if ( pSurface->desc.pixelFormat != image_pixel_format_t::RGBA8_UNORM ) {
        return picasso_document_status_t::UNSUPPORTED_SURFACE;
    }

    const u32 nTilesPerRow =
        ( pSurface->desc.extent.nWidth +
          PICASSO_TEXTURE_HISTORY_TILE_SIZE - 1u ) /
        PICASSO_TEXTURE_HISTORY_TILE_SIZE;
    const u32 nTileRows =
        ( pSurface->desc.extent.nHeight +
          PICASSO_TEXTURE_HISTORY_TILE_SIZE - 1u ) /
        PICASSO_TEXTURE_HISTORY_TILE_SIZE;
    const usize nTileCount =
        static_cast<usize>( nTilesPerRow ) * nTileRows;
    const usize cbCapturedTiles = ( nTileCount + 7u ) / 8u;
    byte *pCapturedTiles = static_cast<byte *>( Allocator_AllocateZeroed(
        pDocument->pAllocator,
        cbCapturedTiles ) );
    if ( pCapturedTiles == nullptr ) {
        return picasso_document_status_t::ALLOCATION_FAILED;
    }

    pDocument->activeEdit.semantic = semantic;
    pDocument->activeEdit.editKind = editKind;
    pDocument->activeEdit.pCapturedTiles = pCapturedTiles;
    pDocument->activeEdit.cbCapturedTiles = cbCapturedTiles;
    pDocument->activeEdit.nTilesPerRow = nTilesPerRow;
    pDocument->activeEdit.bActive = CY_TRUE;
    return picasso_document_status_t::OK;
}

picasso_document_status_t PicassoTextureDocument_ApplyDab(
    picasso_texture_document_t *pDocument,
    const picasso_brush_dab_t &dab ) noexcept
{
    if ( !PicassoTextureDocument_IsInitialized( pDocument ) ) {
        return pDocument == nullptr
            ? picasso_document_status_t::INVALID_ARGUMENT
            : picasso_document_status_t::NOT_INITIALIZED;
    }
    if ( !pDocument->activeEdit.bActive ) {
        return picasso_document_status_t::NO_ACTIVE_EDIT;
    }
    if ( pDocument->activeEdit.editKind ==
             picasso_pixel_edit_kind_t::FLOOD_FILL ||
         ( pDocument->activeEdit.editKind ==
               picasso_pixel_edit_kind_t::BRUSH_STROKE &&
           dab.mode != picasso_brush_mode_t::PAINT ) ||
         ( pDocument->activeEdit.editKind ==
               picasso_pixel_edit_kind_t::ERASER_STROKE &&
           dab.mode != picasso_brush_mode_t::ERASE ) ) {
        return picasso_document_status_t::INVALID_ARGUMENT;
    }

    image_surface_t *pSurface = PicassoTextureSet_GetChannel(
        &pDocument->textureSet,
        pDocument->activeEdit.semantic );
    if ( pSurface == nullptr ) {
        return picasso_document_status_t::PROCESSING_FAILED;
    }
    const image_view_t destination = ImageSurface_GetView( pSurface );
    picasso_pixel_rect_t bounds{};
    picasso_paint_status_t paintStatus = PicassoPaint_DabBounds(
        destination,
        dab,
        &bounds );
    if ( paintStatus != picasso_paint_status_t::OK ) {
        return PicassoTextureDocument_FromPaintStatus( paintStatus );
    }
    if ( !PicassoTextureDocument_CaptureRect(
             *pDocument,
             *pSurface,
             bounds ) ) {
        return picasso_document_status_t::ALLOCATION_FAILED;
    }

    bool_t bChanged = CY_FALSE;
    paintStatus = PicassoPaint_ApplyDab(
        destination,
        dab,
        &bChanged );
    if ( paintStatus == picasso_paint_status_t::OK ) {
        pDocument->activeEdit.bChanged |= bChanged;
    }
    return PicassoTextureDocument_FromPaintStatus( paintStatus );
}

picasso_document_status_t PicassoTextureDocument_EndPixelEdit(
    picasso_texture_document_t *pDocument ) noexcept
{
    if ( !PicassoTextureDocument_IsInitialized( pDocument ) ) {
        return pDocument == nullptr
            ? picasso_document_status_t::INVALID_ARGUMENT
            : picasso_document_status_t::NOT_INITIALIZED;
    }
    if ( !pDocument->activeEdit.bActive ) {
        return picasso_document_status_t::NO_ACTIVE_EDIT;
    }
    if ( !pDocument->activeEdit.bChanged ) {
        PicassoTextureDocument_ReleaseActiveEdit( *pDocument );
        return picasso_document_status_t::NOTHING_TO_COMMIT;
    }

    if ( PicassoTextureSet_MarkChannelChanged(
             &pDocument->textureSet,
             pDocument->activeEdit.semantic ) !=
         picasso_texture_set_status_t::OK ) {
        PicassoTextureDocument_CancelPixelEdit( pDocument );
        return picasso_document_status_t::PROCESSING_FAILED;
    }

    const u64 nBeforeState = pDocument->nCurrentState;
    const u64 nAfterState =
        PicassoTextureDocument_AllocateState( *pDocument );
    pDocument->nCurrentState = nAfterState;

    // The capture bitset is needed only while deciding whether a tile has
    // already been saved. History retains descriptors and byte payload only.
    Allocator_Free(
        pDocument->pAllocator,
        pDocument->activeEdit.pCapturedTiles,
        pDocument->activeEdit.cbCapturedTiles );
    pDocument->activeEdit.pCapturedTiles = nullptr;
    pDocument->activeEdit.cbCapturedTiles = 0u;
    PicassoTextureDocument_PushPixelHistory(
        *pDocument,
        nBeforeState,
        nAfterState );
    PicassoTextureDocument_ReleaseActiveEdit( *pDocument );
    return picasso_document_status_t::OK;
}

void PicassoTextureDocument_CancelPixelEdit(
    picasso_texture_document_t *pDocument ) noexcept
{
    if ( !PicassoTextureDocument_IsInitialized( pDocument ) ||
         !pDocument->activeEdit.bActive ) {
        return;
    }
    picasso_active_pixel_edit_t &edit = pDocument->activeEdit;
    image_surface_t *pSurface = PicassoTextureSet_GetChannel(
        &pDocument->textureSet,
        edit.semantic );
    if ( edit.bChanged && pSurface != nullptr &&
         PicassoTextureDocument_SwapPixelPatches(
             *pSurface,
             edit.pTiles,
             edit.nTileCount,
             edit.pData ) ) {
        (void) PicassoTextureSet_MarkChannelChanged(
            &pDocument->textureSet,
            edit.semantic );
    }
    PicassoTextureDocument_ReleaseActiveEdit( *pDocument );
}

picasso_document_status_t PicassoTextureDocument_FloodFill(
    picasso_texture_document_t *pDocument,
    picasso_channel_semantic_t semantic,
    u32 x,
    u32 y,
    const byte replacement[4] ) noexcept
{
    if ( replacement == nullptr ) {
        return picasso_document_status_t::INVALID_ARGUMENT;
    }
    const picasso_document_status_t beginStatus =
        PicassoTextureDocument_BeginPixelEdit(
            pDocument,
            semantic,
            picasso_pixel_edit_kind_t::FLOOD_FILL );
    if ( beginStatus != picasso_document_status_t::OK ) {
        return beginStatus;
    }

    image_surface_t *pSurface = PicassoTextureSet_GetChannel(
        &pDocument->textureSet,
        semantic );
    if ( x >= pSurface->desc.extent.nWidth ||
         y >= pSurface->desc.extent.nHeight ) {
        PicassoTextureDocument_CancelPixelEdit( pDocument );
        return picasso_document_status_t::INVALID_ARGUMENT;
    }
    const image_view_t destination = ImageSurface_GetView( pSurface );
    const byte *pTarget = ImageView_GetPixel(
        destination,
        x,
        y,
        0u ).pData;
    if ( Cy_MemEqual( pTarget, replacement, 4u ) ) {
        PicassoTextureDocument_CancelPixelEdit( pDocument );
        return picasso_document_status_t::NOTHING_TO_COMMIT;
    }

    const picasso_pixel_rect_t fullSurface{
        0u,
        0u,
        pSurface->desc.extent.nWidth,
        pSurface->desc.extent.nHeight
    };
    if ( !PicassoTextureDocument_CaptureRect(
             *pDocument,
             *pSurface,
             fullSurface ) ) {
        PicassoTextureDocument_CancelPixelEdit( pDocument );
        return picasso_document_status_t::ALLOCATION_FAILED;
    }

    bool_t bChanged = CY_FALSE;
    const picasso_paint_status_t paintStatus = PicassoPaint_FloodFill(
        destination,
        x,
        y,
        replacement,
        pDocument->pAllocator,
        &bChanged );
    pDocument->activeEdit.bChanged = bChanged;
    if ( paintStatus != picasso_paint_status_t::OK ) {
        PicassoTextureDocument_CancelPixelEdit( pDocument );
        return PicassoTextureDocument_FromPaintStatus( paintStatus );
    }
    return PicassoTextureDocument_EndPixelEdit( pDocument );
}

picasso_document_status_t PicassoTextureDocument_Undo(
    picasso_texture_document_t *pDocument ) noexcept
{
    if ( !PicassoTextureDocument_IsInitialized( pDocument ) ) {
        return pDocument == nullptr
            ? picasso_document_status_t::INVALID_ARGUMENT
            : picasso_document_status_t::NOT_INITIALIZED;
    }
    if ( !PicassoTextureDocument_CanUndo( pDocument ) ) {
        return pDocument->activeEdit.bActive
            ? picasso_document_status_t::EDIT_ALREADY_ACTIVE
            : picasso_document_status_t::NOTHING_TO_UNDO;
    }

    const picasso_texture_history_entry_t &entry =
        pDocument->history[pDocument->iHistoryCursor - 1u];
    const picasso_document_status_t status =
        PicassoTextureDocument_ExecuteHistoryEntry(
        *pDocument,
        entry,
        CY_TRUE );
    if ( status == picasso_document_status_t::OK ) {
        --pDocument->iHistoryCursor;
        pDocument->nCurrentState = entry.nBeforeState;
    }
    return status;
}

picasso_document_status_t PicassoTextureDocument_Redo(
    picasso_texture_document_t *pDocument ) noexcept
{
    if ( !PicassoTextureDocument_IsInitialized( pDocument ) ) {
        return pDocument == nullptr
            ? picasso_document_status_t::INVALID_ARGUMENT
            : picasso_document_status_t::NOT_INITIALIZED;
    }
    if ( !PicassoTextureDocument_CanRedo( pDocument ) ) {
        return pDocument->activeEdit.bActive
            ? picasso_document_status_t::EDIT_ALREADY_ACTIVE
            : picasso_document_status_t::NOTHING_TO_REDO;
    }

    const picasso_texture_history_entry_t &entry =
        pDocument->history[pDocument->iHistoryCursor];
    const picasso_document_status_t status =
        PicassoTextureDocument_ExecuteHistoryEntry(
        *pDocument,
        entry,
        CY_FALSE );
    if ( status == picasso_document_status_t::OK ) {
        ++pDocument->iHistoryCursor;
        pDocument->nCurrentState = entry.nAfterState;
    }
    return status;
}

bool_t PicassoTextureDocument_IsOpen(
    const picasso_texture_document_t *pDocument ) noexcept
{
    return PicassoTextureDocument_IsInitialized( pDocument ) &&
           PicassoTextureSet_IsValid( &pDocument->textureSet ) &&
           PicassoTextureSet_HasChannel(
               &pDocument->textureSet,
               picasso_channel_semantic_t::BASE_COLOR );
}

bool_t PicassoTextureDocument_IsDirty(
    const picasso_texture_document_t *pDocument ) noexcept
{
    return PicassoTextureDocument_IsOpen( pDocument ) &&
           pDocument->nCurrentState != pDocument->nSavedState;
}

bool_t PicassoTextureDocument_CanUndo(
    const picasso_texture_document_t *pDocument ) noexcept
{
    return PicassoTextureDocument_IsOpen( pDocument ) &&
           !pDocument->activeEdit.bActive &&
           pDocument->iHistoryCursor > 0u;
}

bool_t PicassoTextureDocument_CanRedo(
    const picasso_texture_document_t *pDocument ) noexcept
{
    return PicassoTextureDocument_IsOpen( pDocument ) &&
           !pDocument->activeEdit.bActive &&
           pDocument->iHistoryCursor < pDocument->nHistoryCount;
}

bool_t PicassoTextureDocument_HasActivePixelEdit(
    const picasso_texture_document_t *pDocument ) noexcept
{
    return PicassoTextureDocument_IsInitialized( pDocument ) &&
           pDocument->activeEdit.bActive;
}

void PicassoTextureDocument_MarkSaved(
    picasso_texture_document_t *pDocument ) noexcept
{
    if ( PicassoTextureDocument_IsOpen( pDocument ) ) {
        pDocument->nSavedState = pDocument->nCurrentState;
    }
}

const_image_view_t PicassoTextureDocument_View(
    const picasso_texture_document_t *pDocument ) noexcept
{
    const image_surface_t *pSurface =
        PicassoTextureDocument_PrimarySurface( pDocument );
    return pSurface != nullptr
        ? ImageSurface_GetView( pSurface )
        : const_image_view_t{};
}

image_surface_t *PicassoTextureDocument_PrimarySurface(
    picasso_texture_document_t *pDocument ) noexcept
{
    return pDocument != nullptr
        ? PicassoTextureSet_GetChannel(
              &pDocument->textureSet,
              picasso_channel_semantic_t::BASE_COLOR )
        : nullptr;
}

const image_surface_t *PicassoTextureDocument_PrimarySurface(
    const picasso_texture_document_t *pDocument ) noexcept
{
    return pDocument != nullptr
        ? PicassoTextureSet_GetChannel(
              &pDocument->textureSet,
              picasso_channel_semantic_t::BASE_COLOR )
        : nullptr;
}

const char *PicassoTextureDocument_OperationName(
    picasso_texture_operation_t operation ) noexcept
{
    switch ( operation ) {
        case picasso_texture_operation_t::FLIP_HORIZONTAL:
            return "Flip Horizontal";
        case picasso_texture_operation_t::FLIP_VERTICAL:
            return "Flip Vertical";
        case picasso_texture_operation_t::ROTATE_90_CLOCKWISE:
            return "Rotate 90 Clockwise";
        case picasso_texture_operation_t::ROTATE_90_COUNTER_CLOCKWISE:
            return "Rotate 90 Counter-clockwise";
        case picasso_texture_operation_t::ROTATE_180:
            return "Rotate 180";
        default:
            return "Unknown Operation";
    }
}

const char *PicassoTextureDocument_HistoryName(
    const picasso_texture_history_entry_t &entry ) noexcept
{
    if ( entry.kind == picasso_history_kind_t::TRANSFORM ) {
        return PicassoTextureDocument_OperationName( entry.operation );
    }
    if ( entry.kind == picasso_history_kind_t::PIXEL_EDIT ) {
        switch ( entry.pixels.editKind ) {
            case picasso_pixel_edit_kind_t::BRUSH_STROKE:
                return "Brush Stroke";
            case picasso_pixel_edit_kind_t::ERASER_STROKE:
                return "Eraser Stroke";
            case picasso_pixel_edit_kind_t::FLOOD_FILL:
                return "Flood Fill";
        }
    }
    return "Unknown Edit";
}

const char *PicassoTextureDocument_StatusName(
    picasso_document_status_t status ) noexcept
{
    switch ( status ) {
        case picasso_document_status_t::OK:                return "OK";
        case picasso_document_status_t::INVALID_ARGUMENT:  return "INVALID_ARGUMENT";
        case picasso_document_status_t::NOT_INITIALIZED:   return "NOT_INITIALIZED";
        case picasso_document_status_t::NO_DOCUMENT:       return "NO_DOCUMENT";
        case picasso_document_status_t::INVALID_CANVAS:    return "INVALID_CANVAS";
        case picasso_document_status_t::ALLOCATION_FAILED: return "ALLOCATION_FAILED";
        case picasso_document_status_t::DECODE_FAILED:     return "DECODE_FAILED";
        case picasso_document_status_t::ENCODE_FAILED:     return "ENCODE_FAILED";
        case picasso_document_status_t::PROCESSING_FAILED: return "PROCESSING_FAILED";
        case picasso_document_status_t::EDIT_ALREADY_ACTIVE: return "EDIT_ALREADY_ACTIVE";
        case picasso_document_status_t::NO_ACTIVE_EDIT:      return "NO_ACTIVE_EDIT";
        case picasso_document_status_t::NOTHING_TO_COMMIT:   return "NOTHING_TO_COMMIT";
        case picasso_document_status_t::UNSUPPORTED_SURFACE: return "UNSUPPORTED_SURFACE";
        case picasso_document_status_t::NOTHING_TO_UNDO:   return "NOTHING_TO_UNDO";
        case picasso_document_status_t::NOTHING_TO_REDO:   return "NOTHING_TO_REDO";
        default:                                            return "UNKNOWN_PICASSO_DOCUMENT_STATUS";
    }
}

} // namespace cypher::tools::picasso
