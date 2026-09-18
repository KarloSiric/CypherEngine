//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileOrthoCamera.h
//  Purpose: Defines the shared world-space state used to link 2D cameras.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_ORTHOCAMERA_H
#define CYPHER_TOOLS_TILEEDITOR_ORTHOCAMERA_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <algorithm>
#include <cmath>

namespace cypher::tools::tile_editor
{

enum tile_ortho_camera_axis_t : unsigned char {
    TILE_ORTHO_CAMERA_AXIS_X = 1u << 0u,
    TILE_ORTHO_CAMERA_AXIS_Y = 1u << 1u,
    TILE_ORTHO_CAMERA_AXIS_Z = 1u << 2u
};

// Center coordinates are expressed in map-world units. axisMask identifies
// which two coordinates belong to the source projection. A linked target
// copies only axes present in both masks, while every target copies scale.
struct tile_ortho_camera_state_t {
    double pixelsPerWorldUnit{ 1.0 };
    double centerX{};
    double centerY{};
    double centerZ{};
    unsigned char axisMask{};
};

struct tile_ortho_camera_scale_range_t {
    double minimum{};
    double maximum{};
};

inline constexpr double TILE_ORTHO_CAMERA_MIN_PIXELS_PER_UNIT = 0.0001;
inline constexpr double TILE_ORTHO_CAMERA_MAX_PIXELS_PER_UNIT = 1024.0;
inline constexpr double TILE_ORTHO_CAMERA_MIN_PIXELS_PER_CELL = 0.05;
inline constexpr double TILE_ORTHO_CAMERA_MAX_PIXELS_PER_CELL = 128.0;
inline constexpr double TILE_ORTHO_CAMERA_BOTTOM_HUD_PIXELS = 24.0;

inline tile_ortho_camera_scale_range_t
TileOrthoCamera_CommonScaleRange( double cellSize )
{
    if ( !std::isfinite( cellSize ) || cellSize <= 0.0 ) cellSize = 1.0;
    return {
        std::max( TILE_ORTHO_CAMERA_MIN_PIXELS_PER_UNIT,
                  TILE_ORTHO_CAMERA_MIN_PIXELS_PER_CELL / cellSize ),
        std::min( TILE_ORTHO_CAMERA_MAX_PIXELS_PER_UNIT,
                  TILE_ORTHO_CAMERA_MAX_PIXELS_PER_CELL / cellSize )
    };
}

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_ORTHOCAMERA_H
