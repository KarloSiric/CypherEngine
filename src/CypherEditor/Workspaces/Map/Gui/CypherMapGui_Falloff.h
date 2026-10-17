//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Falloff.h
//  Purpose: Declares the Displacement brush's falloff: the presets and a
//           hand-drawn curve (`editor.map.displace_curve`), shared by the
//           sculpt dabs and the curve the tool panel edits.
//  Details: A curve is strength (y, 0..1) against distance from the brush
//           centre (x, 0 centre .. 1 edge), as points "x:y" separated by
//           spaces. It always spans x = 0 to 1 and runs through its points
//           as a monotone cubic (Fritsch-Carlson), so it never overshoots
//           between points that rise or fall.
//
//  History:
//  - Created by Karlo Siric on 2026-10-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_FALLOFF_H
#define CYPHER_EDITOR_MAP_GUI_FALLOFF_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier0/CypherCommon_BaseTypes.h"
#include "CypherCommon/Tier1/CypherCommon_StringView.h"

#include <string>
#include <vector>

namespace cypher::editor::map
{

inline constexpr common::usize MAP_FALLOFF_CURVE_MAX_POINTS = 16u;
inline constexpr const char *MAP_FALLOFF_CURVE_DEFAULT = "0:1 0.5:0.5 1:0";

struct map_curve_point_t {
    common::f64 x{ 0.0 };
    common::f64 y{ 0.0 };
};

enum class map_falloff_kind_t : common::u8 { SMOOTH = 0u, LINEAR, SHARP, CONSTANT, CUSTOM };

// Ready to evaluate per vertex: build once per dab.
struct map_falloff_t {
    map_falloff_kind_t kind{ map_falloff_kind_t::SMOOTH };
    std::vector<map_curve_point_t> points{};
    std::vector<common::f64> slopes{};
};

// Sorted by x, clamped to 0..1, ends added at x = 0 and 1 (holding the
// nearest point's strength); unreadable text gives the default curve.
CYPHER_NODISCARD std::vector<map_curve_point_t> MapFalloff_ParseCurve( common::string_view_t text );
CYPHER_NODISCARD std::string MapFalloff_FormatCurve( const std::vector<map_curve_point_t> &points );

// `name` is the falloff setting (smooth, linear, sharp, constant, custom);
// `curve` the curve text, used by custom.
CYPHER_NODISCARD map_falloff_t MapFalloff_Make( common::string_view_t name, common::string_view_t curve );
// Strength at distance t (0 centre .. 1 edge), 0..1.
CYPHER_NODISCARD common::f64 MapFalloff_At( const map_falloff_t &falloff, common::f64 t ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_FALLOFF_H
