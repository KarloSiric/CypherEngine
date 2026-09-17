//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileMapMaterials.h
//  Purpose: Declares the built-in blockout material catalog shared by tools
//           and renderer previews.
//  Details: Maps store compact stable slots. This catalog gives the first
//           authoring milestone deterministic names and preview colors until
//           project material assets provide the same slot mapping.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_TILEMAPMATERIALS_H
#define CYPHER_TOOLS_TILEEDITOR_TILEMAPMATERIALS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherTileMapDocument.h"

namespace cypher::tools::tile_editor
{

struct tile_map_material_definition_t {
    u16 nSlot{ 0u };
    const char *pStableId{ nullptr };
    const char *pDisplayName{ nullptr };
    f32 colorR{ 1.0f };
    f32 colorG{ 1.0f };
    f32 colorB{ 1.0f };
};

// This is intentionally a small blockout palette. Stable IDs are suitable for
// a later project-material manifest; display names are editor presentation.
CYPHER_NODISCARD usize CypherTileMapMaterial_Count() noexcept;

CYPHER_NODISCARD const tile_map_material_definition_t *
CypherTileMapMaterial_At( usize iMaterial ) noexcept;

CYPHER_NODISCARD const tile_map_material_definition_t *
CypherTileMapMaterial_Find( u16 nSlot ) noexcept;

// Unknown slots remain renderable and are deliberately obvious in previews.
CYPHER_NODISCARD tile_map_material_definition_t
CypherTileMapMaterial_Resolve( u16 nSlot ) noexcept;

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_TILEMAPMATERIALS_H
