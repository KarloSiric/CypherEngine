//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Hotspots.h
//  Purpose: Declares hotspot rectangle files: the regions of a trim or
//           panel texture that hotspot texturing fits faces into (Hammer's
//           .rect files).
//  Details: The file sits beside its material: materials/x.cymat has its
//           regions in materials/x.cyrect (schema "cypher.hotspots" 1), so
//           the material format and its cooker are untouched and variants
//           can copy one layout. Each region is a UV rectangle (0..1 across
//           the texture, V down), the world size it was painted for, and
//           whether it may turn a quarter and tile along U or V.
//
//           @cykv 1
//           @schema "cypher.hotspots" 1
//           { rects = [ { uv = [0, 0, 1, 0.25] size = [256, 64] rotate = true tile_u = true tile_v = false } ] }
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_HOTSPOTS_H
#define CYPHER_EDITOR_MAP_HOTSPOTS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"
#include "CypherGeometry_MeshUvTransform.h"

#include <string>
#include <vector>

namespace cypher::editor::map
{

inline constexpr const char *MAP_HOTSPOTS_SCHEMA_ID = "cypher.hotspots";
inline constexpr common::u32 MAP_HOTSPOTS_SCHEMA_VERSION = 1u;
inline constexpr common::usize MAP_HOTSPOTS_TEXT_MAX = 1024u * 1024u;

// The rectangle file of a material: its path with the extension replaced
// by .cyrect (a path with no extension gets one added).
CYPHER_NODISCARD std::string MapHotspots_SidecarPath( common::string_view_t materialPath );

// Reads a rectangle file. ROOT_UNREADABLE for text that is not one;
// ROOT_INVALID for a region that is empty, outside 0..1, or sized <= 0.
CYPHER_NODISCARD map_status_t MapHotspots_Parse( common::string_view_t text, std::vector<geometry::mesh_hotspot_rect_t> &rectsOut );

// Writes regions as a rectangle file into an initialized buffer.
CYPHER_NODISCARD map_status_t MapHotspots_Write( const std::vector<geometry::mesh_hotspot_rect_t> &rects, common::text_buffer_t *pTextOut );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_HOTSPOTS_H
