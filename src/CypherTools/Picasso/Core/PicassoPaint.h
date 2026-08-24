//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Core/PicassoPaint.h
//  Purpose: Declares Qt-independent raster paint operations for Picasso.
//  Details: Paint kernels mutate caller-owned RGBA8 views without allocating
//           hidden image copies. Document history remains a separate concern.
//
//  History:
//  - Created by Karlo Siric on 2026-08-19
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_PICASSO_PAINT_H
#define CYPHER_TOOLS_PICASSO_PAINT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ImageView.h"
#include "CypherCommon_Allocator.h"

namespace cypher::tools::picasso
{

using namespace cypher::common;

enum class picasso_paint_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    UNSUPPORTED_FORMAT,
    ALLOCATION_FAILED
};

enum class picasso_brush_mode_t : u8 {
    PAINT = 0u,
    ERASE
};

// Integer pixel bounds use an exclusive maximum, matching the usual image-row
// iteration contract: [x, x + width) and [y, y + height).
struct picasso_pixel_rect_t {
    u32 x{ 0u };
    u32 y{ 0u };
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
};

// Coordinates are expressed in image space. A pixel at integer (x, y) has its
// center at (x + 0.5, y + 0.5), which keeps subpixel mouse input predictable.
struct picasso_brush_dab_t {
    f32 x{ 0.0f };
    f32 y{ 0.0f };
    f32 nDiameter{ 1.0f };
    f32 opacity{ 1.0f };
    f32 hardness{ 1.0f };
    byte color[4]{ 0u, 0u, 0u, 255u };
    picasso_brush_mode_t mode{ picasso_brush_mode_t::PAINT };
};

// Calculates the clipped area a dab may modify. History captures this region
// before PicassoPaint_ApplyDab writes the first pixel.
CYPHER_NODISCARD picasso_paint_status_t PicassoPaint_DabBounds(
    const image_view_t &destination,
    const picasso_brush_dab_t &dab,
    picasso_pixel_rect_t *pBoundsOut ) noexcept;

// Applies one pressure-independent dab. PAINT uses straight-alpha source-over;
// ERASE reduces alpha while retaining RGB to avoid transparent-edge color loss.
CYPHER_NODISCARD picasso_paint_status_t PicassoPaint_ApplyDab(
    const image_view_t &destination,
    const picasso_brush_dab_t &dab,
    bool_t *pChangedOut ) noexcept;

// Replaces one exact, four-connected RGBA8 region. Scratch storage comes from
// the supplied allocator and is released before the function returns.
CYPHER_NODISCARD picasso_paint_status_t PicassoPaint_FloodFill(
    const image_view_t &destination,
    u32 x,
    u32 y,
    const byte replacement[4],
    const allocator_t *pScratchAllocator,
    bool_t *pChangedOut ) noexcept;

CYPHER_NODISCARD const char *PicassoPaint_StatusName(
    picasso_paint_status_t status ) noexcept;

} // namespace cypher::tools::picasso

#endif // CYPHER_TOOLS_PICASSO_PAINT_H
