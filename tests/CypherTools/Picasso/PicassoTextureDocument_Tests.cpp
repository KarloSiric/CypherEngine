//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherTools/Picasso/PicassoTextureDocument_Tests.cpp
//  Purpose: Tests Picasso's Qt-independent texture document behavior.
//  Details: Coverage protects generated canvases, dirty tracking, reversible
//           transforms, PNG interchange, replacement, and invalid requests.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoTextureDocument.h"

#include "CypherCommon_Allocator.h"
#include "CypherCommon_ImageView.h"
#include "CypherCommon_MemoryOps.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using namespace cypher::common;
using namespace cypher::tools::picasso;

namespace
{

const byte *Pixel(
    const picasso_texture_document_t &document,
    u32 x,
    u32 y ) noexcept
{
    return ImageView_GetPixel(
        PicassoTextureDocument_View( &document ),
        x,
        y,
        0u ).pData;
}

picasso_canvas_desc_t TwoColorRow() noexcept
{
    picasso_canvas_desc_t desc{};
    desc.nWidth = 2u;
    desc.nHeight = 1u;
    desc.fill = picasso_canvas_fill_t::CHECKERBOARD;
    desc.nCheckerSize = 1u;
    desc.colorA[0] = 255u;
    desc.colorA[1] = 0u;
    desc.colorA[2] = 0u;
    desc.colorA[3] = 255u;
    desc.colorB[0] = 0u;
    desc.colorB[1] = 255u;
    desc.colorB[2] = 0u;
    desc.colorB[3] = 255u;
    return desc;
}

picasso_canvas_desc_t SolidCanvas(
    u32 nWidth,
    u32 nHeight,
    byte red,
    byte green,
    byte blue,
    byte alpha = 255u ) noexcept
{
    picasso_canvas_desc_t desc{};
    desc.nWidth = nWidth;
    desc.nHeight = nHeight;
    desc.fill = picasso_canvas_fill_t::SOLID;
    desc.colorA[0] = red;
    desc.colorA[1] = green;
    desc.colorA[2] = blue;
    desc.colorA[3] = alpha;
    return desc;
}

picasso_brush_dab_t HardDab(
    f32 x,
    f32 y,
    f32 nDiameter,
    byte red,
    byte green,
    byte blue,
    byte alpha = 255u ) noexcept
{
    picasso_brush_dab_t dab{};
    dab.x = x;
    dab.y = y;
    dab.nDiameter = nDiameter;
    dab.opacity = 1.0f;
    dab.hardness = 1.0f;
    dab.color[0] = red;
    dab.color[1] = green;
    dab.color[2] = blue;
    dab.color[3] = alpha;
    return dab;
}

} // namespace

TEST_CASE( "Picasso document creates generated canvases and tracks saves",
           "[CypherTools][Picasso][Document][Create]" )
{
    picasso_texture_document_t document{};
    REQUIRE( PicassoTextureDocument_Init(
                 &document,
                 Allocator_GetSystem() ) == picasso_document_status_t::OK );
    REQUIRE_FALSE( PicassoTextureDocument_IsOpen( &document ) );

    REQUIRE( PicassoTextureDocument_Create(
                 &document,
                 TwoColorRow() ) == picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_IsOpen( &document ) );
    REQUIRE( PicassoTextureDocument_IsDirty( &document ) );
    REQUIRE( document.textureSet.extent.nWidth == 2u );
    REQUIRE( document.textureSet.extent.nHeight == 1u );
    REQUIRE( Pixel( document, 0u, 0u )[0] == 255u );
    REQUIRE( Pixel( document, 1u, 0u )[1] == 255u );

    PicassoTextureDocument_MarkSaved( &document );
    REQUIRE_FALSE( PicassoTextureDocument_IsDirty( &document ) );
    PicassoTextureDocument_Shutdown( &document );
}

TEST_CASE( "Picasso document transforms support undo, redo, and branching",
           "[CypherTools][Picasso][Document][History]" )
{
    picasso_texture_document_t document{};
    REQUIRE( PicassoTextureDocument_Init(
                 &document,
                 Allocator_GetSystem() ) == picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_Create(
                 &document,
                 TwoColorRow() ) == picasso_document_status_t::OK );
    PicassoTextureDocument_MarkSaved( &document );

    REQUIRE( PicassoTextureDocument_Apply(
                 &document,
                 picasso_texture_operation_t::FLIP_HORIZONTAL ) ==
             picasso_document_status_t::OK );
    REQUIRE( Pixel( document, 0u, 0u )[1] == 255u );
    REQUIRE( PicassoTextureDocument_IsDirty( &document ) );
    REQUIRE( PicassoTextureDocument_CanUndo( &document ) );

    REQUIRE( PicassoTextureDocument_Undo( &document ) ==
             picasso_document_status_t::OK );
    REQUIRE( Pixel( document, 0u, 0u )[0] == 255u );
    REQUIRE_FALSE( PicassoTextureDocument_IsDirty( &document ) );
    REQUIRE( PicassoTextureDocument_CanRedo( &document ) );

    REQUIRE( PicassoTextureDocument_Redo( &document ) ==
             picasso_document_status_t::OK );
    REQUIRE( Pixel( document, 0u, 0u )[1] == 255u );
    REQUIRE( PicassoTextureDocument_Undo( &document ) ==
             picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_Apply(
                 &document,
                 picasso_texture_operation_t::FLIP_VERTICAL ) ==
             picasso_document_status_t::OK );
    REQUIRE_FALSE( PicassoTextureDocument_CanRedo( &document ) );
}

TEST_CASE( "Picasso document rotations update extent and round-trip",
           "[CypherTools][Picasso][Document][Rotate]" )
{
    picasso_texture_document_t document{};
    REQUIRE( PicassoTextureDocument_Init(
                 &document,
                 Allocator_GetSystem() ) == picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_Create(
                 &document,
                 TwoColorRow() ) == picasso_document_status_t::OK );

    REQUIRE( PicassoTextureDocument_Apply(
                 &document,
                 picasso_texture_operation_t::ROTATE_90_CLOCKWISE ) ==
             picasso_document_status_t::OK );
    REQUIRE( document.textureSet.extent.nWidth == 1u );
    REQUIRE( document.textureSet.extent.nHeight == 2u );
    REQUIRE( Pixel( document, 0u, 0u )[0] == 255u );
    REQUIRE( Pixel( document, 0u, 1u )[1] == 255u );

    REQUIRE( PicassoTextureDocument_Undo( &document ) ==
             picasso_document_status_t::OK );
    REQUIRE( document.textureSet.extent.nWidth == 2u );
    REQUIRE( document.textureSet.extent.nHeight == 1u );
    REQUIRE( Pixel( document, 0u, 0u )[0] == 255u );
    REQUIRE( Pixel( document, 1u, 0u )[1] == 255u );
}

TEST_CASE( "Picasso document transforms every active material channel",
           "[CypherTools][Picasso][Document][Channels]" )
{
    picasso_texture_document_t document{};
    REQUIRE( PicassoTextureDocument_Init(
                 &document,
                 Allocator_GetSystem() ) == picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_Create(
                 &document,
                 TwoColorRow() ) == picasso_document_status_t::OK );
    REQUIRE( PicassoTextureSet_AddDefaultChannel(
                 &document.textureSet,
                 picasso_channel_semantic_t::ROUGHNESS ) ==
             picasso_texture_set_status_t::OK );

    image_surface_t *pRoughness = PicassoTextureSet_GetChannel(
        &document.textureSet,
        picasso_channel_semantic_t::ROUGHNESS );
    REQUIRE( pRoughness != nullptr );
    byte_span_t roughnessRow = ImageView_GetRow(
        ImageSurface_GetView( pRoughness ),
        0u,
        0u );
    roughnessRow.pData[0] = 32u;
    roughnessRow.pData[1] = 224u;
    REQUIRE( PicassoTextureSet_MarkChannelChanged(
                 &document.textureSet,
                 picasso_channel_semantic_t::ROUGHNESS ) ==
             picasso_texture_set_status_t::OK );

    REQUIRE( PicassoTextureDocument_Apply(
                 &document,
                 picasso_texture_operation_t::FLIP_HORIZONTAL ) ==
             picasso_document_status_t::OK );
    REQUIRE( roughnessRow.pData[0] == 224u );
    REQUIRE( roughnessRow.pData[1] == 32u );

    REQUIRE( PicassoTextureDocument_Undo( &document ) ==
             picasso_document_status_t::OK );
    REQUIRE( roughnessRow.pData[0] == 32u );
    REQUIRE( roughnessRow.pData[1] == 224u );
}

TEST_CASE( "Picasso document exports and imports PNG transactionally",
           "[CypherTools][Picasso][Document][Interchange]" )
{
    picasso_texture_document_t source{};
    REQUIRE( PicassoTextureDocument_Init(
                 &source,
                 Allocator_GetSystem() ) == picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_Create(
                 &source,
                 TwoColorRow() ) == picasso_document_status_t::OK );

    blob_t encoded{};
    REQUIRE( PicassoTextureDocument_ExportPng( &source, &encoded ) ==
             picasso_document_status_t::OK );
    REQUIRE( encoded.cbSize > 8u );

    picasso_texture_document_t imported{};
    REQUIRE( PicassoTextureDocument_Init(
                 &imported,
                 Allocator_GetSystem() ) == picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_OpenEncoded(
                 &imported,
                 Blob_Block( &encoded ),
                 { "texture.png", 11u } ) == picasso_document_status_t::OK );
    REQUIRE_FALSE( PicassoTextureDocument_IsDirty( &imported ) );
    REQUIRE( imported.sourceFormat == image_file_format_t::PNG );
    REQUIRE( Cy_MemEqual(
        Pixel( imported, 0u, 0u ),
        Pixel( source, 0u, 0u ),
        4u ) );
    REQUIRE( Cy_MemEqual(
        Pixel( imported, 1u, 0u ),
        Pixel( source, 1u, 0u ),
        4u ) );
}

TEST_CASE( "Picasso document rejects invalid state without mutation",
           "[CypherTools][Picasso][Document][Failure]" )
{
    picasso_texture_document_t document{};
    REQUIRE( PicassoTextureDocument_Create(
                 &document,
                 TwoColorRow() ) == picasso_document_status_t::NOT_INITIALIZED );
    REQUIRE( PicassoTextureDocument_Init(
                 &document,
                 Allocator_GetSystem() ) == picasso_document_status_t::OK );

    picasso_canvas_desc_t invalid = TwoColorRow();
    invalid.nWidth = 0u;
    REQUIRE( PicassoTextureDocument_Create( &document, invalid ) ==
             picasso_document_status_t::INVALID_CANVAS );
    REQUIRE_FALSE( PicassoTextureDocument_IsOpen( &document ) );
    REQUIRE( PicassoTextureDocument_Undo( &document ) ==
             picasso_document_status_t::NOTHING_TO_UNDO );
    REQUIRE( PicassoTextureDocument_Redo( &document ) ==
             picasso_document_status_t::NOTHING_TO_REDO );
}

TEST_CASE( "Picasso brush strokes cross tile boundaries as one undo step",
           "[CypherTools][Picasso][Document][Paint][History]" )
{
    picasso_texture_document_t document{};
    REQUIRE( PicassoTextureDocument_Init(
                 &document,
                 Allocator_GetSystem() ) == picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_Create(
                 &document,
                 SolidCanvas( 130u, 65u, 255u, 255u, 255u ) ) ==
             picasso_document_status_t::OK );
    PicassoTextureDocument_MarkSaved( &document );

    REQUIRE( PicassoTextureDocument_BeginPixelEdit(
                 &document,
                 picasso_channel_semantic_t::BASE_COLOR,
                 picasso_pixel_edit_kind_t::BRUSH_STROKE ) ==
             picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_ApplyDab(
                 &document,
                 HardDab( 63.5f, 63.5f, 5.0f, 220u, 32u, 16u ) ) ==
             picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_EndPixelEdit( &document ) ==
             picasso_document_status_t::OK );

    REQUIRE( document.nHistoryCount == 1u );
    REQUIRE( document.history[0].kind ==
             picasso_history_kind_t::PIXEL_EDIT );
    REQUIRE( document.history[0].pixels.nTileCount == 4u );
    REQUIRE( Pixel( document, 63u, 63u )[0] == 220u );
    REQUIRE( Pixel( document, 64u, 63u )[0] == 220u );
    REQUIRE( Pixel( document, 63u, 64u )[0] == 220u );

    REQUIRE( PicassoTextureDocument_Undo( &document ) ==
             picasso_document_status_t::OK );
    REQUIRE( Pixel( document, 63u, 63u )[0] == 255u );
    REQUIRE( Pixel( document, 64u, 63u )[0] == 255u );
    REQUIRE_FALSE( PicassoTextureDocument_IsDirty( &document ) );

    REQUIRE( PicassoTextureDocument_Redo( &document ) ==
             picasso_document_status_t::OK );
    REQUIRE( Pixel( document, 63u, 63u )[0] == 220u );
    REQUIRE( PicassoTextureDocument_IsDirty( &document ) );
    PicassoTextureDocument_Shutdown( &document );
}

TEST_CASE( "Picasso eraser and cancelled strokes preserve exact pixels",
           "[CypherTools][Picasso][Document][Paint][Cancel]" )
{
    picasso_texture_document_t document{};
    REQUIRE( PicassoTextureDocument_Init(
                 &document,
                 Allocator_GetSystem() ) == picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_Create(
                 &document,
                 SolidCanvas( 8u, 8u, 10u, 20u, 30u ) ) ==
             picasso_document_status_t::OK );

    picasso_brush_dab_t eraser = HardDab(
        3.5f,
        3.5f,
        1.0f,
        0u,
        0u,
        0u );
    eraser.mode = picasso_brush_mode_t::ERASE;
    REQUIRE( PicassoTextureDocument_BeginPixelEdit(
                 &document,
                 picasso_channel_semantic_t::BASE_COLOR,
                 picasso_pixel_edit_kind_t::ERASER_STROKE ) ==
             picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_ApplyDab( &document, eraser ) ==
             picasso_document_status_t::OK );
    REQUIRE( Pixel( document, 3u, 3u )[3] == 0u );

    PicassoTextureDocument_CancelPixelEdit( &document );
    REQUIRE_FALSE( PicassoTextureDocument_HasActivePixelEdit( &document ) );
    REQUIRE( Pixel( document, 3u, 3u )[0] == 10u );
    REQUIRE( Pixel( document, 3u, 3u )[1] == 20u );
    REQUIRE( Pixel( document, 3u, 3u )[2] == 30u );
    REQUIRE( Pixel( document, 3u, 3u )[3] == 255u );
    REQUIRE_FALSE( PicassoTextureDocument_CanUndo( &document ) );
    PicassoTextureDocument_Shutdown( &document );
}

TEST_CASE( "Picasso flood fill is contiguous and reversible",
           "[CypherTools][Picasso][Document][Paint][Fill]" )
{
    picasso_texture_document_t document{};
    REQUIRE( PicassoTextureDocument_Init(
                 &document,
                 Allocator_GetSystem() ) == picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_Create(
                 &document,
                 TwoColorRow() ) == picasso_document_status_t::OK );

    const byte blue[4]{ 0u, 0u, 255u, 255u };
    REQUIRE( PicassoTextureDocument_FloodFill(
                 &document,
                 picasso_channel_semantic_t::BASE_COLOR,
                 0u,
                 0u,
                 blue ) == picasso_document_status_t::OK );
    REQUIRE( Pixel( document, 0u, 0u )[2] == 255u );
    REQUIRE( Pixel( document, 1u, 0u )[1] == 255u );
    REQUIRE( std::string_view(
                 PicassoTextureDocument_HistoryName( document.history[0] ) ) ==
             "Flood Fill" );

    REQUIRE( PicassoTextureDocument_Undo( &document ) ==
             picasso_document_status_t::OK );
    REQUIRE( Pixel( document, 0u, 0u )[0] == 255u );
    REQUIRE( Pixel( document, 1u, 0u )[1] == 255u );
    REQUIRE( PicassoTextureDocument_Redo( &document ) ==
             picasso_document_status_t::OK );
    REQUIRE( Pixel( document, 0u, 0u )[2] == 255u );
    PicassoTextureDocument_Shutdown( &document );
}

TEST_CASE( "Picasso no-op pixel edits do not consume history",
           "[CypherTools][Picasso][Document][Paint][NoOp]" )
{
    picasso_texture_document_t document{};
    REQUIRE( PicassoTextureDocument_Init(
                 &document,
                 Allocator_GetSystem() ) == picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_Create(
                 &document,
                 SolidCanvas( 4u, 4u, 0u, 0u, 0u ) ) ==
             picasso_document_status_t::OK );

    REQUIRE( PicassoTextureDocument_BeginPixelEdit(
                 &document,
                 picasso_channel_semantic_t::BASE_COLOR,
                 picasso_pixel_edit_kind_t::BRUSH_STROKE ) ==
             picasso_document_status_t::OK );
    picasso_brush_dab_t transparent = HardDab(
        1.5f,
        1.5f,
        1.0f,
        255u,
        255u,
        255u,
        0u );
    REQUIRE( PicassoTextureDocument_ApplyDab( &document, transparent ) ==
             picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_EndPixelEdit( &document ) ==
             picasso_document_status_t::NOTHING_TO_COMMIT );
    REQUIRE( document.nHistoryCount == 0u );
    PicassoTextureDocument_Shutdown( &document );
}

TEST_CASE( "Picasso pixel history evicts and branches without sharing ownership",
           "[CypherTools][Picasso][Document][Paint][History][Capacity]" )
{
    picasso_texture_document_t document{};
    REQUIRE( PicassoTextureDocument_Init(
                 &document,
                 Allocator_GetSystem() ) == picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_Create(
                 &document,
                 SolidCanvas( 2u, 2u, 0u, 0u, 0u ) ) ==
             picasso_document_status_t::OK );

    for ( usize iEdit = 0u;
          iEdit < PICASSO_TEXTURE_HISTORY_CAPACITY + 4u;
          ++iEdit ) {
        const byte value = ( iEdit % 2u ) == 0u ? 255u : 1u;
        REQUIRE( PicassoTextureDocument_BeginPixelEdit(
                     &document,
                     picasso_channel_semantic_t::BASE_COLOR,
                     picasso_pixel_edit_kind_t::BRUSH_STROKE ) ==
                 picasso_document_status_t::OK );
        REQUIRE( PicassoTextureDocument_ApplyDab(
                     &document,
                     HardDab( 0.5f, 0.5f, 1.0f, value, 0u, 0u ) ) ==
                 picasso_document_status_t::OK );
        REQUIRE( PicassoTextureDocument_EndPixelEdit( &document ) ==
                 picasso_document_status_t::OK );
    }
    REQUIRE( document.nHistoryCount == PICASSO_TEXTURE_HISTORY_CAPACITY );
    REQUIRE( document.iHistoryCursor == PICASSO_TEXTURE_HISTORY_CAPACITY );

    for ( usize iUndo = 0u; iUndo < 16u; ++iUndo ) {
        REQUIRE( PicassoTextureDocument_Undo( &document ) ==
                 picasso_document_status_t::OK );
    }
    REQUIRE( PicassoTextureDocument_CanRedo( &document ) );

    REQUIRE( PicassoTextureDocument_BeginPixelEdit(
                 &document,
                 picasso_channel_semantic_t::BASE_COLOR,
                 picasso_pixel_edit_kind_t::BRUSH_STROKE ) ==
             picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_ApplyDab(
                 &document,
                 HardDab( 1.5f, 1.5f, 1.0f, 0u, 255u, 0u ) ) ==
             picasso_document_status_t::OK );
    REQUIRE( PicassoTextureDocument_EndPixelEdit( &document ) ==
             picasso_document_status_t::OK );
    REQUIRE_FALSE( PicassoTextureDocument_CanRedo( &document ) );
    PicassoTextureDocument_Shutdown( &document );
}
