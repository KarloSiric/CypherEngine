//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Core/PicassoTextureDocument.h
//  Purpose: Declares Picasso's Qt-independent texture document model.
//  Details: The document owns decoded pixels, tracks saved and edited state,
//           provides deterministic canvas generators, and records reversible
//           transform commands without coupling authoring logic to widgets.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_PICASSO_TEXTUREDOCUMENT_H
#define CYPHER_TOOLS_PICASSO_TEXTUREDOCUMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "PicassoPaint.h"
#include "PicassoTextureSet.h"
#include "CypherCommon_ImageCodec.h"
#include "CypherCommon_ImageProcess.h"

namespace cypher::tools::picasso
{

using namespace cypher::common;

inline constexpr usize PICASSO_TEXTURE_HISTORY_CAPACITY = 256u;
inline constexpr usize PICASSO_TEXTURE_MAX_DECODED_SIZE = 512u * CY_MIB;
inline constexpr usize PICASSO_TEXTURE_HISTORY_BYTE_BUDGET = 256u * CY_MIB;
inline constexpr u32 PICASSO_TEXTURE_HISTORY_TILE_SIZE = 64u;

enum class picasso_document_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    NOT_INITIALIZED,
    NO_DOCUMENT,
    INVALID_CANVAS,
    ALLOCATION_FAILED,
    DECODE_FAILED,
    ENCODE_FAILED,
    PROCESSING_FAILED,
    EDIT_ALREADY_ACTIVE,
    NO_ACTIVE_EDIT,
    NOTHING_TO_COMMIT,
    UNSUPPORTED_SURFACE,
    NOTHING_TO_UNDO,
    NOTHING_TO_REDO
};

enum class picasso_canvas_fill_t : u8 {
    SOLID = 0u,
    CHECKERBOARD
};

// Colors use straight-alpha RGBA8 because a new canvas is immediately suitable
// for display, PNG export, and `.cytex` source authoring.
struct picasso_canvas_desc_t {
    u32 nWidth{ 1024u };
    u32 nHeight{ 1024u };
    picasso_canvas_fill_t fill{ picasso_canvas_fill_t::CHECKERBOARD };
    byte colorA[4]{ 48u, 54u, 58u, 255u };
    byte colorB[4]{ 66u, 73u, 78u, 255u };
    u32 nCheckerSize{ 32u };
};

enum class picasso_texture_operation_t : u8 {
    FLIP_HORIZONTAL = 0u,
    FLIP_VERTICAL,
    ROTATE_90_CLOCKWISE,
    ROTATE_90_COUNTER_CLOCKWISE,
    ROTATE_180
};

enum class picasso_history_kind_t : u8 {
    TRANSFORM = 0u,
    PIXEL_EDIT
};

enum class picasso_pixel_edit_kind_t : u8 {
    BRUSH_STROKE = 0u,
    ERASER_STROKE,
    FLOOD_FILL
};

// One patch stores the compact bytes of one changed 64x64 tile. Offsets remain
// stable when the owning byte buffer grows, unlike direct pointers into it.
struct picasso_pixel_tile_patch_t {
    u32 x{ 0u };
    u32 y{ 0u };
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    usize cbRow{ 0u };
    usize iDataOffset{ 0u };
};

struct picasso_pixel_history_t {
    picasso_channel_semantic_t semantic{
        picasso_channel_semantic_t::BASE_COLOR
    };
    picasso_pixel_edit_kind_t editKind{
        picasso_pixel_edit_kind_t::BRUSH_STROKE
    };
    picasso_pixel_tile_patch_t *pTiles{ nullptr };
    usize nTileCount{ 0u };
    usize nTileCapacity{ 0u };
    byte *pData{ nullptr };
    usize cbData{ 0u };
    usize cbDataCapacity{ 0u };
};

struct picasso_texture_history_entry_t {
    picasso_history_kind_t kind{ picasso_history_kind_t::TRANSFORM };
    picasso_texture_operation_t operation{
        picasso_texture_operation_t::FLIP_HORIZONTAL
    };
    picasso_pixel_history_t pixels{};
    u64 nBeforeState{ 0u };
    u64 nAfterState{ 0u };
};

// A live edit captures each tile before the first write. Commit transfers these
// allocations into history; cancel swaps them back to restore the exact pixels.
struct picasso_active_pixel_edit_t {
    picasso_channel_semantic_t semantic{
        picasso_channel_semantic_t::BASE_COLOR
    };
    picasso_pixel_edit_kind_t editKind{
        picasso_pixel_edit_kind_t::BRUSH_STROKE
    };
    picasso_pixel_tile_patch_t *pTiles{ nullptr };
    usize nTileCount{ 0u };
    usize nTileCapacity{ 0u };
    byte *pData{ nullptr };
    usize cbData{ 0u };
    usize cbDataCapacity{ 0u };
    byte *pCapturedTiles{ nullptr };
    usize cbCapturedTiles{ 0u };
    u32 nTilesPerRow{ 0u };
    bool_t bActive{ CY_FALSE };
    bool_t bChanged{ CY_FALSE };
};

// The texture set is the authoritative channel owner. Picasso 1.0 initially
// presents BASE_COLOR as the canvas, while material painting can add channels
// without introducing a second document or duplicating pixel ownership.
struct picasso_texture_document_t {
    picasso_texture_document_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( picasso_texture_document_t );

    const allocator_t *pAllocator{ nullptr };
    picasso_texture_set_t textureSet{};
    image_file_format_t sourceFormat{ image_file_format_t::UNKNOWN };

    picasso_texture_history_entry_t
        history[PICASSO_TEXTURE_HISTORY_CAPACITY]{};
    usize nHistoryCount{ 0u };
    usize iHistoryCursor{ 0u };
    usize cbHistoryBytes{ 0u };
    picasso_active_pixel_edit_t activeEdit{};

    u64 nCurrentState{ 0u };
    u64 nSavedState{ 0u };
    u64 nNextState{ 1u };
};

CYPHER_NODISCARD
picasso_document_status_t PicassoTextureDocument_Init(
    picasso_texture_document_t *pDocument,
    const allocator_t *pAllocator ) noexcept;

void PicassoTextureDocument_Shutdown(
    picasso_texture_document_t *pDocument ) noexcept;

// Replaces the current document transactionally with a generated RGBA8 canvas.
CYPHER_NODISCARD
picasso_document_status_t PicassoTextureDocument_Create(
    picasso_texture_document_t *pDocument,
    const picasso_canvas_desc_t &desc ) noexcept;

// Imports encoded bytes through the shared Common codec. Path is used only as
// a format hint for signature-less formats such as TGA.
CYPHER_NODISCARD
picasso_document_status_t PicassoTextureDocument_OpenEncoded(
    picasso_texture_document_t *pDocument,
    binary_block_t encoded,
    string_view_t sourcePath ) noexcept;

// Exports the current working surface as PNG. The output blob must be canonical
// empty and receives ownership only after a complete successful encode.
CYPHER_NODISCARD
picasso_document_status_t PicassoTextureDocument_ExportPng(
    const picasso_texture_document_t *pDocument,
    blob_t *pEncodedOut ) noexcept;

CYPHER_NODISCARD
picasso_document_status_t PicassoTextureDocument_Apply(
    picasso_texture_document_t *pDocument,
    picasso_texture_operation_t operation ) noexcept;

// Brush strokes are explicit transactions so a drag containing hundreds of
// dabs becomes one undo step. Call CancelPixelEdit after any failed dab.
CYPHER_NODISCARD
picasso_document_status_t PicassoTextureDocument_BeginPixelEdit(
    picasso_texture_document_t *pDocument,
    picasso_channel_semantic_t semantic,
    picasso_pixel_edit_kind_t editKind ) noexcept;

CYPHER_NODISCARD
picasso_document_status_t PicassoTextureDocument_ApplyDab(
    picasso_texture_document_t *pDocument,
    const picasso_brush_dab_t &dab ) noexcept;

CYPHER_NODISCARD
picasso_document_status_t PicassoTextureDocument_EndPixelEdit(
    picasso_texture_document_t *pDocument ) noexcept;

void PicassoTextureDocument_CancelPixelEdit(
    picasso_texture_document_t *pDocument ) noexcept;

// Flood fill is a complete one-shot transaction and therefore does not require
// a separate Begin/End pair from callers.
CYPHER_NODISCARD
picasso_document_status_t PicassoTextureDocument_FloodFill(
    picasso_texture_document_t *pDocument,
    picasso_channel_semantic_t semantic,
    u32 x,
    u32 y,
    const byte replacement[4] ) noexcept;

CYPHER_NODISCARD
picasso_document_status_t PicassoTextureDocument_Undo(
    picasso_texture_document_t *pDocument ) noexcept;

CYPHER_NODISCARD
picasso_document_status_t PicassoTextureDocument_Redo(
    picasso_texture_document_t *pDocument ) noexcept;

CYPHER_NODISCARD bool_t PicassoTextureDocument_IsOpen(
    const picasso_texture_document_t *pDocument ) noexcept;

CYPHER_NODISCARD bool_t PicassoTextureDocument_IsDirty(
    const picasso_texture_document_t *pDocument ) noexcept;

CYPHER_NODISCARD bool_t PicassoTextureDocument_CanUndo(
    const picasso_texture_document_t *pDocument ) noexcept;

CYPHER_NODISCARD bool_t PicassoTextureDocument_CanRedo(
    const picasso_texture_document_t *pDocument ) noexcept;

CYPHER_NODISCARD bool_t PicassoTextureDocument_HasActivePixelEdit(
    const picasso_texture_document_t *pDocument ) noexcept;

void PicassoTextureDocument_MarkSaved(
    picasso_texture_document_t *pDocument ) noexcept;

CYPHER_NODISCARD const_image_view_t PicassoTextureDocument_View(
    const picasso_texture_document_t *pDocument ) noexcept;

// BASE_COLOR is the primary 2D canvas until a layer compositor publishes a
// separate display surface. These accessors avoid exposing texture-set layout.
CYPHER_NODISCARD image_surface_t *PicassoTextureDocument_PrimarySurface(
    picasso_texture_document_t *pDocument ) noexcept;

CYPHER_NODISCARD const image_surface_t *PicassoTextureDocument_PrimarySurface(
    const picasso_texture_document_t *pDocument ) noexcept;

CYPHER_NODISCARD const char *PicassoTextureDocument_OperationName(
    picasso_texture_operation_t operation ) noexcept;

CYPHER_NODISCARD const char *PicassoTextureDocument_HistoryName(
    const picasso_texture_history_entry_t &entry ) noexcept;

CYPHER_NODISCARD const char *PicassoTextureDocument_StatusName(
    picasso_document_status_t status ) noexcept;

} // namespace cypher::tools::picasso

#endif // CYPHER_TOOLS_PICASSO_TEXTUREDOCUMENT_H
