//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileGrid.h
//  Purpose: Shares display-grid spacing rules between map views.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_GRID_H
#define CYPHER_TOOLS_TILEEDITOR_GRID_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>

namespace cypher::tools::tile_editor
{

// Display spacing never changes authored cell metrics or tool snapping.
// Adaptive views use 1, 2, 4, 8... cells; fixed views honor the exact setting.
inline int TileEditorGrid_EffectiveSpacingCells(
    int minimumCells,
    double pixelsPerCell,
    bool bAdaptive,
    int minimumPixels ) noexcept
{
    minimumCells = std::max( 1, minimumCells );
    if ( !bAdaptive ) return minimumCells;

    constexpr int maximumStep = 1 << 30;
    minimumCells = std::min( minimumCells, maximumStep );
    int step = 1;
    while ( step < minimumCells ) step *= 2;
    if ( !std::isfinite( pixelsPerCell ) || pixelsPerCell <= 0.0 ) return step;
    const double targetPixels = std::max( 1, minimumPixels );
    while ( step < maximumStep && step * pixelsPerCell < targetPixels ) step *= 2;
    return step;
}

// Preserve an obvious minor/major hierarchy after adaptive spacing grows past
// the authored major interval. Lines remain anchored to world zero. An LCM
// also handles fixed non-power-of-two spacing without drifting major lines.
inline double TileEditorGrid_EffectiveMajorSpacingCells(
    int spacingCells, int majorEvery ) noexcept
{
    const auto spacing = static_cast<std::int64_t>( std::max( 1, spacingCells ) );
    const auto interval = static_cast<std::int64_t>( std::max( 2, majorEvery ) );
    const auto common = std::lcm( spacing, interval );
    return static_cast<double>( common > spacing ? common : spacing * interval );
}

// Classify authored cell/level coordinates, never the index of a displayed
// line. Major positions remain anchored to zero after zooming or panning.
inline bool TileEditorGrid_IsMajorCoordinate(
    double cellCoordinate,
    double majorEvery ) noexcept
{
    if ( !std::isfinite( cellCoordinate ) || !std::isfinite( majorEvery ) || majorEvery <= 0 ) return false;
    return std::abs( std::remainder( cellCoordinate, majorEvery ) ) < 0.000001;
}

// Coordinate labels start on major-grid positions, then become more sparse
// until adjacent labels have enough screen space. The returned interval uses
// the same units as baseSpacing and always remains anchored to world zero.
inline double TileEditorGrid_EffectiveRulerSpacing(
    double baseSpacing,
    double pixelsPerUnit,
    double minimumPixels ) noexcept
{
    if ( !std::isfinite( baseSpacing ) || baseSpacing <= 0.0 ) return 1.0;
    if ( !std::isfinite( pixelsPerUnit ) || pixelsPerUnit <= 0.0 ) return baseSpacing;

    const double targetPixels = std::isfinite( minimumPixels )
        ? std::max( 1.0, minimumPixels ) : 1.0;
    const double requiredSpacing = targetPixels / pixelsPerUnit;
    double spacing = baseSpacing;
    while ( spacing < requiredSpacing &&
            spacing <= std::numeric_limits<double>::max() * 0.5 ) {
        spacing *= 2.0;
    }
    return spacing;
}

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_GRID_H
