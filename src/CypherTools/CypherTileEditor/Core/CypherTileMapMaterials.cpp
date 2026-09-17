//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileMapMaterials.cpp
//  Purpose: Defines the deterministic first-milestone blockout palette.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileMapMaterials.h"

namespace cypher::tools::tile_editor
{

namespace
{

constexpr tile_map_material_definition_t BLOCKOUT_MATERIALS[]{
    { 0u, "cypher.blockout.neutral", "Blockout Neutral", 0.48f, 0.55f, 0.58f },
    { 1u, "cypher.blockout.concrete", "Cool Concrete", 0.48f, 0.50f, 0.53f },
    { 2u, "cypher.blockout.steel", "Industrial Steel", 0.28f, 0.38f, 0.43f },
    { 3u, "cypher.blockout.hazard", "Hazard Yellow", 0.82f, 0.58f, 0.13f },
    { 4u, "cypher.blockout.trim", "Dark Trim", 0.16f, 0.19f, 0.22f },
    { 5u, "cypher.blockout.exterior", "Exterior Stone", 0.43f, 0.40f, 0.36f },
    { 6u, "cypher.blockout.accent_blue", "Cypher Blue", 0.10f, 0.52f, 0.73f },
    { 7u, "cypher.blockout.accent_orange", "Cypher Orange", 0.92f, 0.39f, 0.10f }
};

} // namespace

usize CypherTileMapMaterial_Count() noexcept
{
    return sizeof( BLOCKOUT_MATERIALS ) / sizeof( BLOCKOUT_MATERIALS[0] );
}

const tile_map_material_definition_t *CypherTileMapMaterial_At(
    usize iMaterial ) noexcept
{
    return iMaterial < CypherTileMapMaterial_Count()
        ? BLOCKOUT_MATERIALS + iMaterial
        : nullptr;
}

const tile_map_material_definition_t *CypherTileMapMaterial_Find(
    u16 nSlot ) noexcept
{
    for ( const tile_map_material_definition_t &material :
          BLOCKOUT_MATERIALS ) {
        if ( material.nSlot == nSlot ) return &material;
    }
    return nullptr;
}

tile_map_material_definition_t CypherTileMapMaterial_Resolve(
    u16 nSlot ) noexcept
{
    const tile_map_material_definition_t *pMaterial =
        CypherTileMapMaterial_Find( nSlot );
    if ( pMaterial != nullptr ) return *pMaterial;
    return {
        nSlot,
        "cypher.blockout.unknown",
        "Unknown Material",
        0.92f,
        0.08f,
        0.72f
    };
}

} // namespace cypher::tools::tile_editor
